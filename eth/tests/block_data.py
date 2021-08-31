#!/usr/bin/env python3

# Copyright (C) 2021 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Tests basic metadata associated with blocks.
"""


import ethtest

from xayax.eth import uintToXaya
from xayax.testcase import ZmqSubscriber


if __name__ == "__main__":
  with ethtest.Fixture () as f:
    f.generate (20)
    f.syncBlocks ()

    sub = ZmqSubscriber (f.zmqCtx, f.env.getXRpcUrl (), "game")
    sub.subscribe ("game-block-attach")
    with sub.run ():
      [blkHash] = f.generate (1)
      blk = f.w3.eth.get_block ("latest")
      f.assertEqual (uintToXaya (blk["hash"].hex ()), blkHash)
      _, data = sub.receive ()

      f.assertEqual (data, {
        "block": {
          "hash": blkHash,
          "parent": uintToXaya (blk["parentHash"].hex ()),
          "height": blk["number"],
          "timestamp": blk["timestamp"],
          "rngseed": blkHash,
        },
        "admin": [],
        "moves": [],
      })
