#!/usr/bin/env python3

# Copyright (C) 2021 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Tests the situation of multiple moves being sent from a single
transaction (with a custom contract).
"""


import ethtest

from xayax.eth import uintToXaya
from xayax.testcase import ZmqSubscriber

import json


# Custom contract that allows sending multiple moves with
# a single transaction.
CONTRACT = """
  pragma solidity >=0.8.0;

  interface IMover
  {
    function move (string memory ns, string memory name, string memory mv,
                   uint256 nonce, uint256 amount, address receiver)
        external returns (uint256);
  }

  contract MultiMoves
  {

    IMover public immutable mover;

    constructor (IMover mv)
    {
      mover = mv;
    }

    function send (string[] memory ns, string[] memory names,
                   string[] memory mv) public
    {
      for (uint i = 0; i < ns.length; ++i)
        for (uint j = 0; j < names.length; ++j)
          for (uint k = 0; k < mv.length; ++k)
            mover.move (ns[i], names[j], mv[k],
                        type (uint256).max, 0, address (0));
    }

  }
"""


if __name__ == "__main__":
  with ethtest.Fixture () as f:
    contracts = f.env.contracts
    contracts.multi = f.env.ganache.deployCode (contracts.account, CONTRACT,
                                                contracts.registry.address)

    contracts.registry.functions\
        .setApprovalForAll (contracts.multi.address, True)\
        .transact ({"from": contracts.account})

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
        contracts.multi.functions.send (["p"], ["game", "domob"], [
          json.dumps ({"g": {"game": 2}}),
          json.dumps ({"g": {"game": 3}}),
        ]).transact ({"from": contracts.account, "gas": 500_000}),
        contracts.multi.functions.send (["p", "g"], ["game"], [
          json.dumps ({"g": {"game": 4}, "cmd": 2}),
        ]).transact ({"from": contracts.account, "gas": 500_000}),
        contracts.multi.functions.send (["g"], ["game"], [
          json.dumps ({"cmd": 3}),
          json.dumps ({"cmd": 4}),
        ]).transact ({"from": contracts.account, "gas": 500_000}),
        f.sendMove ("p/domob", {"g": {"game": 42}}),
        f.sendMove ("g/game", {"cmd": 100}),
      ]

      f.generate (1)
      _, data = sub.receive ()

      f.assertEqual (data["admin"], [
        {
          "txid": uintToXaya (ids[1].hex ()),
          "cmd": 1,
          "burnt": 0,
          "out": {},
        },
        {
          "txid": uintToXaya (ids[3].hex ()),
          "cmd": 2,
          "burnt": 0,
          "out": {},
        },
        {
          "txid": uintToXaya (ids[4].hex ()),
          "cmd": 3,
          "burnt": 0,
          "out": {},
        },
        {
          "txid": uintToXaya (ids[4].hex ()),
          "cmd": 4,
          "burnt": 0,
          "out": {},
        },
        {
          "txid": uintToXaya (ids[6].hex ()),
          "cmd": 100,
          "burnt": 0,
          "out": {},
        },
      ])
      f.assertEqual (data["moves"], [
        {
          "txid": uintToXaya (ids[0].hex ()),
          "name": "domob",
          "move": 1,
          "burnt": 0,
          "out": {},
        },
        {
          "txid": uintToXaya (ids[2].hex ()),
          "name": "game",
          "move": 2,
          "burnt": 0,
          "out": {},
        },
        {
          "txid": uintToXaya (ids[2].hex ()),
          "name": "game",
          "move": 3,
          "burnt": 0,
          "out": {},
        },
        {
          "txid": uintToXaya (ids[2].hex ()),
          "name": "domob",
          "move": 2,
          "burnt": 0,
          "out": {},
        },
        {
          "txid": uintToXaya (ids[2].hex ()),
          "name": "domob",
          "move": 3,
          "burnt": 0,
          "out": {},
        },
        {
          "txid": uintToXaya (ids[3].hex ()),
          "name": "game",
          "move": 4,
          "burnt": 0,
          "out": {},
        },
        {
          "txid": uintToXaya (ids[5].hex ()),
          "name": "domob",
          "move": 42,
          "burnt": 0,
          "out": {},
        },
      ])
