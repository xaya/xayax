#!/usr/bin/env python3

# Copyright (C) 2021 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Tests basic metadata associated with blocks.
"""


import coretest

from xayax.testcase import ZmqSubscriber


if __name__ == "__main__":
  with coretest.Fixture () as f:
    f.generate (20)
    f.syncBlocks ()

    sub = ZmqSubscriber (f.zmqCtx, f.env.getXRpcUrl (), "game")
    sub.subscribe ("game-block-attach")
    with sub.run ():
      [blk] = f.generate (1)
      coreData = f.env.createCoreRpc ().getblock (blk, 1)
      _, data = sub.receive ()

      f.assertEqual (data, {
        "block": {
          "hash": blk,
          "parent": coreData["previousblockhash"],
          "height": coreData["height"],
          "timestamp": coreData["time"],
          "mediantime": coreData["mediantime"],
          "rngseed": coreData["rngseed"],
        },
        "admin": [],
        "moves": [],
      })
