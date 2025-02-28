# Copyright (C) 2021-2024 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Utilities for running Xaya X connected to an Ethereum node from Python,
e.g. for integration tests.
"""


from xayagametest import xaya

import jsonrpclib
from eth_account import Account, messages
from web3 import Web3

import base64
import codecs
from contextlib import contextmanager
import json
import logging
import os
import os.path
import shutil
import subprocess
import time


# Maximum uint256 value, which is used in the contract interface
# (e.g. for approvals and the move nonce).
maxUint256 = 2**256 - 1

# Zero address.
zeroAddr = "0x" + "00" * 20


def loadJsonData (name):
  """
  Loads a JSON data file with the given name, assumed to be in the same
  directory as the Python file itself.
  """

  path = os.path.dirname (os.path.abspath (__file__))
  fileName = os.path.join (path, name)

  # If we are running inside an automake job, the data files may actually
  # be in the build directory (while the script itself is in the source).
  # Check there as well.
  if not os.path.exists (fileName):
    top_builddir = os.getenv ("top_builddir")
    if top_builddir is not None:
      fileName = os.path.join (top_builddir, "xayax", name)

  with open (fileName, "rt") as f:
    return json.load (f)


def uintToXaya (value):
  """
  Converts a hex literal for an uint256 from Ethereum format
  (possibly with 0x prefix) to the Xaya format without.
  """

  if len (value) == 66:
    assert value[:2] == "0x"
    return value[2:]

  assert len (value) == 64
  return value


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

  def start (self, accountsContract, ethrpc, ws=None, watchForPending=[]):
    """
    Starts the process, waiting until its RPC interface is up.
    """

    if self.proc is not None:
      self.log.error ("Xaya X process is already running, not starting again")
      return

    self.log.info ("Starting new Xaya X process")
    args = list (self.binaryCmd)
    args.append ("--accounts_contract=%s" % accountsContract)
    args.append ("--eth_rpc_url=%s" % ethrpc)
    if ws is not None:
      args.append ("--eth_ws_url=%s" % ws)
    args.append ("--port=%d" % self.port)
    args.append ("--zmq_address=tcp://127.0.0.1:%d" % self.zmqPort)
    args.append ("--datadir=%s" % self.datadir)
    args.append ("--watch_for_pending_moves=%s" % ",".join (watchForPending))
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


class EvmNode:
  """
  A process running a local Ethereum-like blockchain for testing.
  """

  def __init__ (self, basedir, rpcPort):
    """
    Initialises / configures a fresh instance without starting it.
    """

    self.log = logging.getLogger ("xayax.eth")
    self.basedir = basedir

    self.port = rpcPort
    self.rpcurl = "http://localhost:%d" % self.port
    self.wsurl = "ws://localhost:%d" % self.port

    self.proc = None

  def start (self):
    """
    Starts the process, waiting until its RPC interface is up.
    """

    if self.proc is not None:
      self.log.error ("EVM node process is already running, not starting again")
      return

    self.log.info ("Starting new EVM node process")
    args = ["/usr/bin/env", "anvil"]
    args.extend (["-p", str (self.port)])
    # By default, Anvil mines a new block on each transaction sent.
    # This is not what we want for testing in a Xaya-like environment;
    # tests will mine on demand instead.
    args.append ("--no-mining")
    # Use a timestamp "early" into Xaya history.  This ensures that the
    # genesis block will be before anything programmed into a particular
    # game, as would be the case with a real network.
    args.extend (["--timestamp", "1514764800"])
    self.logFile = open (os.path.join (self.basedir, "anvil.log"), "wt")
    self.proc = subprocess.Popen (args, stderr=subprocess.STDOUT,
                                  stdout=self.logFile)

    # The EVM node wrapper has an optional mock time, which can be set
    # from the outside and which (if set) is used as argument to evm_mine
    # to determine the next block's timestamp.
    self.mockTime = None

    self.rpc = self.createRpc ()
    self.log.info ("Waiting for the JSON-RPC server to be up...")
    while True:
      try:
        chainId = int (self.rpc.eth_chainId (), 16)
        self.log.info ("EVM node is up, chain = %d" % chainId)
        break
      except:
        time.sleep (0.1)

    self.w3 = Web3 (Web3.HTTPProvider (self.rpcurl))

  def stop (self):
    if self.proc is None:
      self.log.error ("No EVM node process is running, cannot stop it")
      return

    if self.logFile is not None:
      self.logFile.close ()
      self.logFile = None

    self.w3 = None

    self.log.info ("Stopping EVM node process")
    self.proc.terminate ()

    self.log.info ("Waiting for process to stop...")
    self.proc.wait ()
    self.proc = None

  def createRpc (self):
    """
    Returns a freshly created JSON-RPC connection for this node.  This can
    be used if multiple threads need to send RPCs in parallel.
    """

    # Anvil does not accept the default application/json-rpc content type
    # sent by jsonrpclib, and wants application/json as content type.
    config = jsonrpclib.config.DEFAULT
    config.content_type = "application/json"

    return jsonrpclib.ServerProxy (self.rpcurl, config=config)

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

  def mine (self):
    """
    Mines a block, taking mock time into account (if set).
    """

    if self.mockTime is None:
      self.rpc.evm_mine ()
    else:
      self.rpc.evm_mine (self.mockTime)

  def deployContract (self, addr, data, *args, **kwargs):
    """
    Deploys a contract with given JSON data (containing the ABI and
    bytecode) to the test network.  The deployment will be done from
    the given address.  Returned is a contract instance for
    the newly deployed contract.
    """

    code = None
    if "bytecode" in data:
      if type (data["bytecode"]) == str:
        code = data["bytecode"]
      elif type (data["bytecode"]) == dict and "object" in data["bytecode"]:
        code = data["bytecode"]["object"]
    assert code is not None, "contract has no bytecode"

    c = self.w3.eth.contract (abi=data["abi"], bytecode=code)

    txid = c.constructor (*args, **kwargs).transact ({"from": addr})
    self.mine ()
    tx = self.w3.eth.wait_for_transaction_receipt (txid)

    return self.w3.eth.contract (abi=data["abi"], address=tx.contractAddress)

  def deployXaya (self):
    """
    Deploys the Xaya contracts (WCHI, accounts registry and policy).

    This method returns an object with the deployer address (owns all WCHI
    and the contracts) and the deployed contract instances.
    """

    class XayaDeployment:
      account = None
      wchi = None
      registry = None
      policy = None
      metadata = None

    res = XayaDeployment ()
    res.account = self.w3.eth.accounts[0]

    res.wchi = self.deployContract (res.account, loadJsonData ("WCHI.json"))
    res.metadata = self.deployContract (res.account,
                                        loadJsonData ("NftMetadata.json"))
    res.policy = self.deployContract (
        res.account, loadJsonData ("XayaPolicy.json"),
        res.metadata.address, 100_0000)
    res.registry = self.deployContract (
        res.account, loadJsonData ("XayaAccounts.json"),
        res.wchi.address, res.policy.address)

    # Approve the account registry for spending unlimited WCHI from each
    # of our internal accounts.
    for a in self.w3.eth.accounts:
      res.wchi.functions.approve (res.registry.address, maxUint256)\
          .transact ({"from": a})
    self.mine ()

    return res


class ChainSnapshot:
  """
  A snapshot of the EVM test blockchain.  It can be created from the
  current state, and then we can always revert back to it at later points
  in time as desired.

  This is a wrapper around the debug evm_snapshot and evm_revert RPC methods,
  which takes care of the underlying snapshot ID and also allows us to
  revert back to a snapshot multiple times.

  Note that reverting to a snapshot invalidates any snapshots taken after,
  i.e. it is not possible to snapshot some tip, revert blocks, and then
  revert back to the tip ("restore" it).
  """

  def __init__ (self, rpc):
    self.rpc = rpc
    self.takeSnapshot ()

  def takeSnapshot (self):
    self.id = self.rpc.evm_snapshot ()

  def restore (self):
    self.rpc.evm_revert (self.id)
    # The node allows a snapshot to be used only once.  Thus take a new one
    # now (at the same state) so we can revert again in the future.
    self.takeSnapshot ()


class Environment:
  """
  A full test environment consisting of a local Ethereum chain
  with the Xaya contracts deployed, and a Xaya X process connected to it.

  When running, it exposes the RPC interface of the Xaya X process, which
  should be connected to the GSP, and also the RPC interface of the
  EVM node process (through JSON-RPC as well as Web3.py) for controlling
  of the environment in a test.
  """

  def __init__ (self, basedir, portgen, xayaxBinary):
    zmqPorts = {
      "hashblock": next (portgen),
    }
    self.evm = EvmNode (basedir, next (portgen))
    self.xnode = Instance (basedir, portgen, xayaxBinary)
    self.log = logging.getLogger ("xayax.eth")
    self.watchForPending = []
    self.defaultPending = False

    # Users of this class can register their own callback closures for
    # between starting of the test chain and Xaya X.  This can be used
    # e.g. to deploy custom constracts as needed, and then add them
    # to the watchForPending.
    self.deploymentCbs = []

  def enablePending (self):
    """
    Sets a flag that indicates that upon starting the environment,
    the auto-deployed accounts contract should be tracked for pending
    moves by default.
    """

    self.defaultPending = True

  def addWatchedContract (self, addr):
    """
    Adds the given contract address as being watched for pending moves once
    the instance is started.
    """

    self.watchForPending.append (addr)

  def addDeploymentCallback (self, cb):
    """
    Adds the given cb to be invoked (with the environment passed as
    single argument) when starting, between running the Ethereum chain
    and starting Xaya X.  This can do custom extra deployment steps needed
    on the chain before starting the test.
    """

    self.deploymentCbs.append (cb)

  @contextmanager
  def run (self):
    """
    Starts up the Xaya Core and connected Xaya X processes, managed
    as a context manager.
    """

    with self.evm.run ():
      self.signerAccounts = {}
      self.contracts = self.evm.deployXaya ()
      self.log.info ("WCHI contract: %s" % self.contracts.wchi.address)
      self.log.info ("Accounts contract: %s" % self.contracts.registry.address)
      self.evm.w3.eth.default_account = self.contracts.account
      self.clearRegisteredCache ()
      if self.defaultPending:
        self.addWatchedContract (self.contracts.registry.address)
      for cb in self.deploymentCbs:
        cb (self)
      with self.xnode.run (self.contracts.registry.address, self.evm.rpcurl,
                           ws=self.evm.wsurl,
                           watchForPending=self.watchForPending):
        yield self

  def createEvmRpc (self):
    """
    Returns a fresh RPC handle for talking directly to the EVM node,
    e.g. for sending moves, mining or triggering reorgs.
    """

    return self.evm.createRpc ()

  def getXRpcUrl (self):
    """
    Returns the RPC URL for the Xaya X instance, which is what GSPs
    should be connected to.
    """

    return self.xnode.rpcurl

  def getGspArguments (self):
    return self.getXRpcUrl (), ["--xaya_rpc_protocol=2"]

  def snapshot (self):
    """
    Returns a chain snapshot that can be used to restore the current state
    at a later time (e.g. for testing reorgs).
    """

    return ChainSnapshot (self.createEvmRpc ())

  def setMockTime (self, timestamp):
    """
    Sets the mock time, which is the timestamp that any blocks mined
    in the future will use.
    """

    self.log.info ("Setting mocktime for EVM node to %d" % timestamp)
    self.evm.mockTime = timestamp

  def clearRegisteredCache (self):
    """
    Removes the in-memory cache for names that have been registered
    and may be pending.  This can be used after a chain reorg to unmark
    names as registered if their registration was actually undone.
    """

    self.registered = set ()

  # Methods from xaya.Environment for managing the blockchain, as
  # needed to run xayagametest instances against this as environment.

  def generate (self, num):
    blks = []
    while len (blks) < num:
      self.evm.mine ()
      blk, _ = self.getChainTip ()
      blks.append (blk)
    return blks

  def getChainTip (self):
    data = self.evm.w3.eth.get_block ("latest")
    return uintToXaya (data["hash"].hex ()), data["number"]

  def createSignerAddress (self):
    account = Account.create ()
    self.signerAccounts[account.address] = account
    return account.address

  def signMessage (self, addr, msg):
    account = self.lookupSignerAccount (addr)
    assert account is not None, "%s is not a signer address" % addr
    full = "Xaya signature for chain %d:\n\n%s" \
              % (self.evm.w3.eth.chain_id, msg)
    encoded = messages.encode_defunct (text=full)
    rawSgn = account.sign_message (encoded).signature
    return codecs.decode (base64.b64encode (rawSgn), "ascii")

  def lookupSignerAccount (self, addr):
    """
    Returns the eth_accounts.Account instance that corresponds to the
    given address.  The address must have been returned previously
    from createSignerAddress.  This can be used if direct access to the
    private keys is needed, for instance.
    """

    if addr not in self.signerAccounts:
      return None

    return self.signerAccounts[addr]

  def nameExists (self, ns, nm):
    # We use an in-memory register of names that have been registered,
    # so that we know if a registration may be pending at the moment.
    if (ns, nm) in self.registered:
      return True
    return self.contracts.registry.functions.exists (ns, nm).call ()

  def register (self, ns, nm, addr=None):
    if addr is None:
      addr = self.contracts.account
    self.registered.add ((ns, nm))
    txid = self.contracts.registry.functions.register (ns, nm)\
              .transact ({"from": addr})
    return uintToXaya (txid.hex ())

  def move (self, ns, nm, strval, addr=None, send=None):
    if addr is None:
      addr = self.contracts.account

    if send is None:
      recipient = zeroAddr
      amount = 0
    else:
      (recipient, amount) = send

    # When sending a move right after registering a name (while the registration
    # may not yet have confirmed), estimate_gas fails.  Thus we provide our
    # own gas limit here.
    txid = self.contracts.registry.functions\
              .move (ns, nm, strval, maxUint256, amount, recipient)\
              .transact ({"from": addr, "gas": 500_000})
    return uintToXaya (txid.hex ())
