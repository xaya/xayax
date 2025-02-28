# Copyright (C) 2025 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
An extended XayaGameTest class from libxayagame, which adds support for
(easily) running XayaGameTest instances off Xaya X.
"""

from . import core, eth

from xayagametest import testcase

from contextlib import contextmanager

XCORE_BINARY_DEFAULT = "/usr/local/bin/xayax-core"
XETH_BINARY_DEFAULT = "/usr/local/bin/xayax-eth"


class XayaXGameTest (testcase.XayaGameTest):
  """
  Extended class for GSP test cases (from xayagametest), which adds
  Xaya X binaries to the arguments and has helper methods for running
  the Xaya X environments as base chain.
  """

  def __init__ (self, gameId, gameBinaryDefault):
    super (XayaXGameTest, self).__init__ (gameId, gameBinaryDefault)

  def addArguments (self, parser):
    parser.add_argument ("--xcore_binary", default=XCORE_BINARY_DEFAULT,
                         help="xayax-core binary to use")
    parser.add_argument ("--xeth_binary", default=XETH_BINARY_DEFAULT,
                         help="xayax-eth binary to use")

  @contextmanager
  def runXayaXCoreEnvironment (self):
    """
    Runs a base-chain environment that uses Xaya X to link back to
    a real Xaya Core instance.
    """

    if self.zmqPending != "one socket":
      raise AssertionError ("Xaya-X-Core only supports one-socket pending")

    env = core.Environment (self.basedir, self.ports,
                            self.args.xayad_binary, self.args.xcore_binary)

    with env.run ():
      self.xayanode = env.xayanode
      self.rpc.xaya = env.xayanode.rpc
      yield env

  @contextmanager
  def runXayaXEthEnvironment (self):
    """
    Runs a base-chain environment that uses Xaya X to link to an
    Ethereum-like test chain.
    """

    if self.zmqPending != "one socket":
      raise AssertionError ("Xaya-X-Eth only supports one-socket pending")

    env = eth.Environment (self.basedir, self.ports, self.args.xeth_binary)
    env.enablePending ()

    with env.run ():
      self.ethnode = env.evm
      self.contracts = env.contracts
      self.rpc.eth = env.createEvmRpc ()
      self.w3 = env.evm.w3
      yield env
