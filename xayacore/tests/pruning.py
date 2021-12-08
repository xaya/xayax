#!/usr/bin/env python3

# Copyright (C) 2021 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Tests requests for blocks that have been pruned.
"""


import coretest

from xayax.testcase import ZmqSubscriber

import jsonrpclib


class PruningFixture (coretest.Fixture):

  def __init__ (self, depth):
    super ().__init__ ()
    self.maxReorgDepth = depth

  def getXayaXExtraArgs (self):
    return ["--max_reorg_depth=%d" % self.maxReorgDepth]


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
