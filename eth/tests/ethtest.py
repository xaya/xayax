# Copyright (C) 2021 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Test fixture for testing the Xaya-X-on-Ethereum connector.
"""


from xayax import eth, testcase

from xayagametest import premine

from contextlib import contextmanager
import os
import os.path


class Fixture (testcase.BaseChainFixture):

  def addArguments (self, parser):
    parser.add_argument ("--xeth_binary", default="",
                         help="xayax-eth binary to use")

  @contextmanager
  def environment (self):
    with super ().environment ():
      self.w3 = self.env.ganache.w3
      yield

  def createBaseChain (self):
    xethBin = self.args.xeth_binary
    if not xethBin:
      top_builddir = os.getenv ("top_builddir")
      if top_builddir is None:
        top_builddir = "../.."
      xethBin = os.path.join (top_builddir, "eth", "xayax-eth")

    env = eth.Environment (self.basedir, self.portgen, xethBin)
    return env.run ()
