#!/usr/bin/env python3

# Copyright (C) 2021-2022 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Tests the situation of multiple moves being sent from a single
transaction (with a custom contract).
"""


import ethtest

from moves_data import mvid

from xayax.eth import uintToXaya
from xayax.testcase import ZmqSubscriber

import json


if __name__ == "__main__":
  with ethtest.Fixture () as f:
    contracts = f.env.contracts
    contracts.multi = f.deployMultiMover ()

    f.env.register ("p", "domob")
    f.env.register ("p", "game")
    f.env.register ("g", "game")
    f.generate (1)
    f.syncBlocks ()

    sub = ZmqSubscriber (f.zmqCtx, f.env.getXRpcUrl (), "game")
    sub.subscribe ("game-block-attach")
    with sub.run ():
      ids = [
        f.sendMove ("p/domob", {"g": {"game": 1}}),
        f.sendMove ("g/game", {"cmd": 1}),
        uintToXaya (
          contracts.multi.functions.send (["p"], ["game", "domob"], [
            json.dumps ({"g": {"game": 2}}),
            json.dumps ({"g": {"game": 3}}),
          ]).transact ({"from": contracts.account, "gas": 500_000}).hex ()
        ),
        uintToXaya (
          contracts.multi.functions.send (["p", "g"], ["game"], [
            json.dumps ({"g": {"game": 4}, "cmd": 2}),
          ]).transact ({"from": contracts.account, "gas": 500_000}).hex ()
        ),
        uintToXaya (
          contracts.multi.functions.send (["g"], ["game"], [
            json.dumps ({"cmd": 3}),
            json.dumps ({"cmd": 4}),
          ]).transact ({"from": contracts.account, "gas": 500_000}).hex ()
        ),
        f.sendMove ("p/domob", {"g": {"game": 42}}),
        f.sendMove ("g/game", {"cmd": 100}),
      ]

      f.generate (1)
      _, data = sub.receive ()

      f.assertEqual (data["admin"], [
        {
          "txid": ids[1],
          "cmd": 1,
          "mvid": mvid (f, "g", "game", 0),
          "burnt": 0,
          "out": {},
        },
        {
          "txid": ids[3],
          "cmd": 2,
          "mvid": mvid (f, "g", "game", 1),
          "burnt": 0,
          "out": {},
        },
        {
          "txid": ids[4],
          "cmd": 3,
          "mvid": mvid (f, "g", "game", 2),
          "burnt": 0,
          "out": {},
        },
        {
          "txid": ids[4],
          "cmd": 4,
          "mvid": mvid (f, "g", "game", 3),
          "burnt": 0,
          "out": {},
        },
        {
          "txid": ids[6],
          "cmd": 100,
          "mvid": mvid (f, "g", "game", 4),
          "burnt": 0,
          "out": {},
        },
      ])
      f.assertEqual (data["moves"], [
        {
          "txid": ids[0],
          "name": "domob",
          "move": 1,
          "mvid": mvid (f, "p", "domob", 0),
          "burnt": 0,
          "out": {},
        },
        {
          "txid": ids[2],
          "name": "game",
          "move": 2,
          "mvid": mvid (f, "p", "game", 0),
          "burnt": 0,
          "out": {},
        },
        {
          "txid": ids[2],
          "name": "game",
          "move": 3,
          "mvid": mvid (f, "p", "game", 1),
          "burnt": 0,
          "out": {},
        },
        {
          "txid": ids[2],
          "name": "domob",
          "move": 2,
          "mvid": mvid (f, "p", "domob", 1),
          "burnt": 0,
          "out": {},
        },
        {
          "txid": ids[2],
          "name": "domob",
          "move": 3,
          "mvid": mvid (f, "p", "domob", 2),
          "burnt": 0,
          "out": {},
        },
        {
          "txid": ids[3],
          "name": "game",
          "move": 4,
          "mvid": mvid (f, "p", "game", 2),
          "burnt": 0,
          "out": {},
        },
        {
          "txid": ids[5],
          "name": "domob",
          "move": 42,
          "mvid": mvid (f, "p", "domob", 3),
          "burnt": 0,
          "out": {},
        },
      ])
