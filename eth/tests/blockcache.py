#!/usr/bin/env python3

# Copyright (C) 2023 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Tests that Xaya X runs and works with the (in memory) block cache enabled.
"""


import ethtest

from xayax.testcase import ZmqSubscriber

import jsonrpclib


class MemoryCacheFixture (ethtest.Fixture):
  """
  Test fixture that runs Xaya X with an in-memory block cache enabled
  and a given max reorg depth (which also affects which blocks get cached).
  """

  def __init__ (self, depth, blockRange):
    super ().__init__ ()
    self.depth = depth
    self.blockRange = blockRange

  def getXayaXExtraArgs (self):
    return [
        "--blockcache_memory",
        "--max_reorg_depth=%d" % self.depth,
        "--xayax_block_range=%d" % self.blockRange,
    ]


if __name__ == "__main__":
  with MemoryCacheFixture (depth=5, blockRange=3) as f:
    sub = ZmqSubscriber (f.zmqCtx, f.env.getXRpcUrl (), "game")
    sub.subscribe ("game-block-attach")
    with sub.run ():
      xrpc = jsonrpclib.ServerProxy (f.env.getXRpcUrl ())

      blocks = f.generate (20)
      f.assertZmqBlocks (sub, "attach", blocks)

      # Request multiple times the blocks.  This will use a cache for the
      # earlier steps and in the later requests (but all should return
      # the correct set of blocks in the end).
      for _ in range (5):
        startInd = 0
        while True:
          data = xrpc.game_sendupdates (gameid="game",
                                        fromblock=blocks[startInd])
          f.assertEqual (data["steps"]["detach"], 0)
          endInd = startInd + data["steps"]["attach"]
          f.assertZmqBlocks (sub, "attach", blocks[startInd + 1 : endInd + 1],
                             reqtoken=data["reqtoken"])
          if data["toblock"] == blocks[-1]:
            f.assertEqual (endInd + 1, len (blocks))
            break
          startInd = endInd
