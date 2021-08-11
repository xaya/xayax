# Copyright (C) 2021 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Test fixture for testing the Xaya-X-on-Xaya-Core connector.
"""


from xayax import core, testcase

import jsonrpclib
from xayagametest import premine
import zmq

from contextlib import contextmanager
import json
import os
import os.path
import time


XAYAD_BINARY_DEFAULT = "/usr/local/bin/xayad"


class Fixture (testcase.Fixture):

  def addArguments (self, parser):
    parser.add_argument ("--xayad_binary", default=XAYAD_BINARY_DEFAULT,
                         help="xayad binary to use")
    parser.add_argument ("--xcore_binary", default="",
                         help="xayax-core binary to use")

  @contextmanager
  def environment (self):
    xcoreBin = self.args.xcore_binary
    if not xcoreBin:
      top_builddir = os.getenv ("top_builddir")
      if top_builddir is None:
        top_builddir = "../.."
      xcoreBin = os.path.join (top_builddir, "core", "xayax-core")

    with super ().environment (), \
         core.Environment (self.basedir, self.portgen,
                           self.args.xayad_binary, xcoreBin).run () as env:
      self.env = env
      premine.collect (self.env.createCoreRpc (), logger=self.log)
      self.syncBlocks ()
      yield

  def generate (self, n):
    """
    Generates n new blocks on the underlying Xaya Core.
    """

    rpc = self.env.createCoreRpc ()
    addr = rpc.getnewaddress ()
    return rpc.generatetoaddress (n, addr)

  def syncBlocks (self):
    """
    Waits for the Xaya X instance to be on the same best block hash
    as Xaya Core.
    """

    core = self.env.createCoreRpc ()
    x = jsonrpclib.ServerProxy (self.env.getXRpcUrl ())
    while core.getbestblockhash () != x.getblockchaininfo ()["bestblockhash"]:
      time.sleep (0.1)

  def sendMove (self, name, data, options={}):
    """
    Sends a move (name_register or name_update) with the given name
    and the data given as JSON.
    """

    core = self.env.createCoreRpc ()
    dataStr = json.dumps (data)

    try:
      return core.name_update (name, dataStr, options)
    except:
      return core.name_register (name, dataStr, options)
