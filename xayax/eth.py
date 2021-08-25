# Copyright (C) 2021 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Utilities for running Xaya X connected to an Ethereum node from Python,
e.g. for integration tests.
"""


from xayagametest import xaya

import jsonrpclib
from web3 import Web3

from contextlib import contextmanager
import logging
import os
import os.path
import shutil
import subprocess
import time


class Instance:
  """
  An instance of the Xaya-X-on-Ethereum process.  The "run" method
  starts it up and manages it as a context manager, which is how it should
  most likely be used.
  """

  def __init__ (self, basedir, portgen, binary):
    """
    Initialises / configures a fresh instance without starting it.
    portgen should be a generator that yields free ports for using
    them, e.g. for RPC and ZMQ.
    """

    self.log = logging.getLogger ("xayax.eth")
    self.datadir = os.path.join (basedir, "xayax-eth")
    self.binary = binary

    self.port = next (portgen)
    self.zmqPort = next (portgen)
    self.rpcurl = "http://localhost:%d" % self.port

    if isinstance (binary, list):
      self.binaryCmd = binary
    else:
      assert isinstance (binary, str)
      self.binaryCmd = [binary]

    self.log.info ("Creating fresh data directory for Xaya X Ethereum in %s"
                    % self.datadir)
    shutil.rmtree (self.datadir, ignore_errors=True)
    os.mkdir (self.datadir)

    self.proc = None

  def start (self, ethrpc):
    """
    Starts the process, waiting until its RPC interface is up.
    """

    if self.proc is not None:
      self.log.error ("Xaya X process is already running, not starting again")
      return

    self.log.info ("Starting new Xaya X process")
    args = list (self.binaryCmd)
    args.append ("--eth_rpc_url=%s" % ethrpc)
    args.append ("--port=%d" % self.port)
    args.append ("--zmq_address=tcp://127.0.0.1:%d" % self.zmqPort)
    args.append ("--datadir=%s" % self.datadir)
    args.append ("--genesis_height=0")
    args.append ("--sanity_checks")
    envVars = dict (os.environ)
    envVars["GLOG_log_dir"] = self.datadir
    self.proc = subprocess.Popen (args, env=envVars)

    self.rpc = self.createRpc ()
    self.log.info ("Waiting for the JSON-RPC server to be up...")
    while True:
      try:
        data = self.rpc.getblockchaininfo ()
        self.log.info ("Xaya X is up, chain = %s" % data["chain"])
        break
      except:
        time.sleep (0.1)

  def stop (self):
    if self.proc is None:
      self.log.error ("No Xaya X process is running, cannot stop it")
      return

    self.log.info ("Stopping Xaya X process")
    self.rpc._notify.stop ()

    self.log.info ("Waiting for process to stop...")
    self.proc.wait ()
    self.proc = None

  def createRpc (self):
    """
    Returns a freshly created JSON-RPC connection for this node.  This can
    be used if multiple threads need to send RPCs in parallel.
    """

    return jsonrpclib.ServerProxy (self.rpcurl)

  @contextmanager
  def run (self, *args, **kwargs):
    """
    Runs this instance as per start/stop (with arguments passed on to start),
    inside a context manager.
    """

    try:
      self.start (*args, **kwargs)
      yield self
    finally:
      self.stop ()


class Ganache:
  """
  A process running a local Ethereum-like blockchain using ganache-cli
  for testing.
  """

  def __init__ (self, basedir, rpcPort):
    """
    Initialises / configures a fresh instance without starting it.
    """

    self.log = logging.getLogger ("xayax.eth")
    self.datadir = os.path.join (basedir, "ganache-cli")

    self.port = rpcPort
    self.rpcurl = "http://localhost:%d" % self.port

    self.log.info ("Creating fresh data directory for Ganache-CLI in %s"
                    % self.datadir)
    shutil.rmtree (self.datadir, ignore_errors=True)
    os.mkdir (self.datadir)

    self.proc = None

  def start (self):
    """
    Starts the process, waiting until its RPC interface is up.
    """

    if self.proc is not None:
      self.log.error ("Ganache process is already running, not starting again")
      return

    self.log.info ("Starting new Ganache-CLI process")
    args = ["/usr/bin/env", "ganache-cli"]
    args.extend (["-p", str (self.port)])
    args.extend (["--db", self.datadir])
    # By default, Ganache mines a new block on each transaction sent.
    # This is not what we want for testing in a Xaya-like environment; thus
    # we set a very long block interval, which will be so long that no blocks
    # get actually mined automatically.  Tests can mine on demand instead.
    args.extend (["-b", str (1_000_000)])
    self.logFile = open (os.path.join (self.datadir, "ganache.log"), "wt")
    self.proc = subprocess.Popen (args, stderr=subprocess.STDOUT,
                                  stdout=self.logFile)

    self.rpc = self.createRpc ()
    self.log.info ("Waiting for the JSON-RPC server to be up...")
    while True:
      try:
        chainId = int (self.rpc.eth_chainId (), 16)
        self.log.info ("Ganache is up, chain = %d" % chainId)
        break
      except:
        time.sleep (0.1)

    self.w3 = Web3 (Web3.HTTPProvider (self.rpcurl))

  def stop (self):
    if self.proc is None:
      self.log.error ("No Ganache process is running, cannot stop it")
      return

    if self.logFile is not None:
      self.logFile.close ()
      self.logFile = None

    self.w3 = None

    self.log.info ("Stopping Ganache process")
    self.proc.terminate ()

    self.log.info ("Waiting for process to stop...")
    self.proc.wait ()
    self.proc = None

  def createRpc (self):
    """
    Returns a freshly created JSON-RPC connection for this node.  This can
    be used if multiple threads need to send RPCs in parallel.
    """

    return jsonrpclib.ServerProxy (self.rpcurl)

  @contextmanager
  def run (self, *args, **kwargs):
    """
    Runs this instance as per start/stop (with arguments passed on to start),
    inside a context manager.
    """

    try:
      self.start (*args, **kwargs)
      yield self
    finally:
      self.stop ()


class Environment:
  """
  A full test environment consisting of a local Ethereum chain
  (using ganache-cli) with the Xaya contracts deployed, and a Xaya X
  process connected to it.
  
  When running, it exposes the RPC interface of the Xaya X process, which
  should be connected to the GSP, and also the RPC interface of the
  ganache-cli process (through JSON-RPC as well as Web3.py) for controlling
  of the environment in a test.
  """

  def __init__ (self, basedir, portgen, xayaxBinary):
    zmqPorts = {
      "hashblock": next (portgen),
    }
    self.ganache = Ganache (basedir, next (portgen))
    self.xnode = Instance (basedir, portgen, xayaxBinary)

  @contextmanager
  def run (self):
    """
    Starts up the Xaya Core and connected Xaya X processes, managed
    as a context manager.
    """

    with self.ganache.run (), \
         self.xnode.run (self.ganache.rpcurl):
      # TODO: Deploy Xaya contracts.
      yield self

  def createGanacheRpc (self):
    """
    Returns a fresh RPC handle for talking directly to the Ganache-CLI
    node, e.g. for sending moves, mining or triggering reorgs.
    """

    return self.ganache.createRpc ()

  def getXRpcUrl (self):
    """
    Returns the RPC URL for the Xaya X instance, which is what GSPs
    should be connected to.
    """

    return self.xnode.rpcurl

  def getGspArguments (self):
    return self.getXRpcUrl (), ["--xaya_rpc_protocol=2"]

  # Methods from xaya.Environment for managing the blockchain, as
  # needed to run xayagametest instances against this as environment.

  def generate (self, num):
    blks = []
    while len (blks) < num:
      self.ganache.rpc.evm_mine ()
      blk, _ = self.getChainTip ()
      blks.append (blk)
    return blks

  def getChainTip (self):
    data = self.ganache.w3.eth.get_block ("latest")
    return data["hash"].hex (), data["number"]

  def nameExists (self, ns, nm):
    assert False

  def register (self, ns, nm):
    assert False

  def move (self, ns, nm, strval, *args, **kwargs):
    assert False
