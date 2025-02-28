# Copyright (C) 2021-2025 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Utilities for working with Xaya X and the connector to Xaya Core from
Python, e.g. for regression testing.
"""


from . import rpcproxy

from xayagametest import xaya

import jsonrpclib

from contextlib import contextmanager
import logging
import os
import os.path
import shutil
import subprocess
import time


class Instance:
  """
  An instance of the Xaya-X-on-Xaya-Core process.  The "run" method
  starts it up and manages it as a context manager, which is how it should
  most likely be used.
  """

  def __init__ (self, basedir, portgen, binary, dirname="xayax-core"):
    """
    Initialises / configures a fresh instance without starting it.
    portgen should be a generator that yields free ports for using
    them, e.g. for RPC and ZMQ.
    """

    self.log = logging.getLogger ("xayax.core")
    self.datadir = os.path.join (basedir, dirname)
    self.binary = binary

    self.port = next (portgen)
    self.zmqPort = next (portgen)
    self.rpcurl = "http://localhost:%d" % self.port

    if isinstance (binary, list):
      self.binaryCmd = binary
    else:
      assert isinstance (binary, str)
      self.binaryCmd = [binary]

    self.log.info ("Creating fresh data directory for Xaya X Core in %s"
                    % self.datadir)
    shutil.rmtree (self.datadir, ignore_errors=True)
    os.mkdir (self.datadir)

    self.proc = None

  def start (self, xayarpc):
    """
    Starts the process, waiting until its RPC interface is up.
    """

    if self.proc is not None:
      self.log.error ("Xaya X process is already running, not starting again")
      return

    self.log.info ("Starting new XayaX process")
    args = list (self.binaryCmd)
    args.append ("--core_rpc_url=%s" % xayarpc)
    args.append ("--port=%d" % self.port)
    args.append ("--zmq_address=tcp://127.0.0.1:%d" % self.zmqPort)
    args.append ("--datadir=%s" % self.datadir)
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


class Environment:
  """
  A full test environment consisting of a real Xaya Core instance
  and a Xaya X process connected to it.  When running, it exposes
  two RPC interfaces for testing:  One of the actual Xaya Core instance,
  which can be used to write things (like moves or mining of blocks),
  and one of the Xaya X process, which should be connected to the GSP.

  The RPC connection between Xaya X and Xaya Core is proxied, so that
  tests can check what happens if it breaks somehow temporarily.
  """

  def __init__ (self, basedir, portgen, coreBinary, xayaxBinary):
    zmqPort = next (portgen)
    zmqPorts = {
      "hashblock": zmqPort,
      "rawtx": zmqPort,
    }
    self.env = xaya.Environment (basedir, next (portgen), zmqPorts,
                                 coreBinary)
    self.xayanode = self.env.node
    self.xnode = Instance (basedir, portgen, xayaxBinary)
    self.proxyPort = next (portgen)

  @contextmanager
  def run (self):
    """
    Starts up the Xaya Core and connected Xaya X processes, managed
    as a context manager.
    """

    with self.env.run ():
      xayaRpcUrl, _ = self.xayanode.getWalletRpc ("")
      self.proxy = rpcproxy.Proxy (("localhost", self.proxyPort), xayaRpcUrl)
      with self.proxy.run (), \
           self.xnode.run ("http://localhost:%d" % self.proxyPort):
        yield self
      self.proxy = None

  def createCoreRpc (self):
    """
    Returns a fresh RPC handle for talking directly to the Xaya Core
    node, e.g. for sending moves, mining or triggering reorgs.
    """

    _, res = self.xayanode.getWalletRpc ("")
    return res

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
    return self.env.generate (num)

  def createSignerAddress (self):
    return self.env.createSignerAddress ()

  def signMessage (self, addr, msg):
    return self.env.signMessage (addr, msg)

  def getChainTip (self):
    return self.env.getChainTip ()

  def nameExists (self, ns, nm):
    return self.env.nameExists (ns, nm)

  def register (self, ns, nm):
    return self.env.register (ns, nm)

  def move (self, ns, nm, strval, *args, **kwargs):
    return self.env.move (ns, nm, strval, *args, **kwargs)
