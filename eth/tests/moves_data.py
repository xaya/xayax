#!/usr/bin/env python3

# Copyright (C) 2021 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Tests that moves are correctly recognised from the contract logs
and returned with the expected data.
"""


import ethtest

from xayax.testcase import ZmqSubscriber


if __name__ == "__main__":
  with ethtest.Fixture () as f:
    sub = ZmqSubscriber (f.zmqCtx, f.env.getXRpcUrl (), "game")
    sub.subscribe ("game-block-attach")
    with sub.run ():
      addr1 = f.w3.eth.accounts[1]
      addr2 = f.w3.eth.accounts[2]

      f.sendMove ("x/foo", {
        "g": {
          "game": {"foo": "bar"},
        },
        "cmd": "something",
      })
      txid1 = f.sendMove ("p/domob", {
        "g": {
          "x": "abc",
          "game": [1, 2, 3],
        },
        "cmd": "ignored"
      })
      f.sendMove ("g/abc", {"cmd": 42})
      txid2 = f.sendMove ("g/game", {"cmd": "admin"}, send=(addr1, 1234_5678))
      txid3 = f.sendMove ("p/andy", {
        "g": {"game": {}},
      }, send=(addr2, 42_0000_0000))

      f.generate (1)
      _, data = sub.receive ()
      del data["block"]

      f.assertEqual (data, {
        "admin": [
          {
            "txid": txid2.hex (),
            "cmd": "admin",
            "burnt": 0,
            "out": {addr1.lower (): 0.12345678},
          },
        ],
        "moves": [
          {
            "txid": txid1.hex (),
            "name": "domob",
            "move": [1, 2, 3],
            "burnt": 0,
            "out": {},
          },
          {
            "txid": txid3.hex (),
            "name": "andy",
            "move": {},
            "burnt": 0,
            "out": {addr2.lower (): 42},
          },
        ],
      })
