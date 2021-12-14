#!/usr/bin/env python3

# Copyright (C) 2021 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Tests requests for blocks that have been pruned.
"""


import coretest

from xayax import core
from xayax.testcase import ZmqSubscriber

import jsonrpclib

import time


class PruningFixture (coretest.Fixture):

  def __init__ (self, depth):
    super ().__init__ ()
    self.maxReorgDepth = depth

  def getXayaXExtraArgs (self):
    return ["--max_reorg_depth=%d" % self.maxReorgDepth]


def waitForBlock (f, xcore, blk):
  """
  Waits until the given Xaya X Core instance is synced up to the
  given block as best tip.
  """

  f.log.info ("Waiting for best block %s on Xaya X..." % blk)
  rpc = xcore.createRpc ()
  while True:
    info = rpc.getblockchaininfo ()
    if info["bestblockhash"] == blk:
      return
    time.sleep (0.01)


if __name__ == "__main__":
  with PruningFixture (5) as f:
    sub = ZmqSubscriber (f.zmqCtx, f.env.getXRpcUrl (), "game")
    sub.subscribe ("game-block-attach")
    sub.subscribe ("game-block-detach")
    with sub.run ():
      rpc = f.env.createCoreRpc ()
      xrpc = jsonrpclib.ServerProxy (f.env.getXRpcUrl ())

      base = f.generate (10)
      _, baseHeight = f.env.getChainTip ()
      f.assertZmqBlocks (sub, "attach", base)

      branch1 = f.generate (5)
      f.assertZmqBlocks (sub, "attach", branch1)

      # Start a second Xaya X instance, which will quick-sync to the
      # branch and then be stopped again.
      xcoreCmd = [f.getXCoreBinary (), "--max_reorg_depth=5"]
      secondXCore = core.Instance (f.basedir, f.portgen, xcoreCmd,
                                   dirname="xayax-core-2")
      with secondXCore.run (f.env.xayanode.rpcurl):
        waitForBlock (f, secondXCore, branch1[-1])

      rpc.invalidateblock (branch1[0])
      branch2 = f.generate (20)
      f.assertZmqBlocks (sub, "detach", branch1[::-1])
      f.assertZmqBlocks (sub, "attach", branch2)

      f.assertEqual (xrpc.getblockhash (height=baseHeight+5), branch2[4])
      f.assertEqual (xrpc.getblockheader (blockhash=branch2[4])["height"],
                     baseHeight + 5)
      f.assertEqual (xrpc.getblockheader (blockhash=branch1[4])["height"],
                     baseHeight + 5)
      f.assertRaises (xrpc.getblockhash, height=baseHeight + 21)
      f.assertRaises (xrpc.getblockheader, blockhash="unknown")

      data = xrpc.game_sendupdates (gameid="game", fromblock=branch1[-1])
      f.assertEqual (data["steps"], {
        "detach": 5,
        "attach": 20,
      })
      f.assertEqual (data["toblock"], branch2[-1])
      f.assertZmqBlocks (sub, "detach", branch1[::-1],
                         reqtoken=data["reqtoken"])
      f.assertZmqBlocks (sub, "attach", branch2, reqtoken=data["reqtoken"])

      data = xrpc.game_sendupdates (gameid="game", fromblock=branch2[9])
      f.assertEqual (data["steps"], {
        "detach": 0,
        "attach": 10,
      })
      f.assertEqual (data["toblock"], branch2[-1])
      f.assertZmqBlocks (sub, "attach", branch2[10:], reqtoken=data["reqtoken"])

      # Start the second instance again to make sure it can correctly
      # catch up, and then also supply the blocks from the previous fork
      # back to the new tip.
      with secondXCore.run (f.env.xayanode.rpcurl):
        waitForBlock (f, secondXCore, branch2[-1])
        sub2 = ZmqSubscriber (f.zmqCtx, secondXCore.rpcurl, "game")
        sub2.subscribe ("game-block-attach")
        sub2.subscribe ("game-block-detach")
        with sub2.run ():
          xrpc2 = secondXCore.createRpc ()
          data = xrpc2.game_sendupdates (gameid="game", fromblock=branch1[-1])
          f.assertEqual (data["steps"], {
            "detach": 5,
            "attach": 20,
          })
          f.assertEqual (data["toblock"], branch2[-1])
          f.assertZmqBlocks (sub2, "detach", branch1[::-1],
                             reqtoken=data["reqtoken"])
          f.assertZmqBlocks (sub2, "attach", branch2, reqtoken=data["reqtoken"])
