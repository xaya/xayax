# Copyright (C) 2021 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Test fixture for testing the Xaya-X-on-Xaya-Core connector.
"""


from xayax import core, testcase

from xayagametest import premine

from contextlib import contextmanager
import os
import os.path


XAYAD_BINARY_DEFAULT = "/usr/local/bin/xayad"


class Fixture (testcase.BaseChainFixture):

  def addArguments (self, parser):
    parser.add_argument ("--xayad_binary", default=XAYAD_BINARY_DEFAULT,
                         help="xayad binary to use")
    parser.add_argument ("--xcore_binary", default="",
                         help="xayax-core binary to use")

  def getXayaXExtraArgs (self):
    return []

  @contextmanager
  def environment (self):
    with super ().environment ():
      premine.collect (self.env.createCoreRpc (), logger=self.log)
      self.syncBlocks ()
      yield

  def getXCoreBinary (self):
    if self.args.xcore_binary:
      return self.args.xcore_binary

    top_builddir = os.getenv ("top_builddir")
    if top_builddir is None:
      top_builddir = "../.."

    return os.path.join (top_builddir, "xayacore", "xayax-core")

  def createBaseChain (self):
    xcoreBin = self.getXCoreBinary ()
    xcoreCmd = [xcoreBin] + self.getXayaXExtraArgs ()
    env = core.Environment (self.basedir, self.portgen,
                            self.args.xayad_binary, xcoreCmd)
    return env.run ()
