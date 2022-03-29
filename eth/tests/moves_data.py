#!/usr/bin/env python3

# Copyright (C) 2021-2022 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Tests that moves are correctly recognised from the contract logs
and returned with the expected data.
"""


import ethtest

from xayax.testcase import ZmqSubscriber


def mvid (f, ns, nm, nonce):
  """
  Extracts the move log for ns/name/nonce through the web3 and
  contracts instances in the passed test fixture, and returns the
  associated mvid (by hashing the ABI-encoded log data).
  """

  sgn = "Move(string,string,string,uint256,uint256,address,uint256,address)"
  fpr = f.w3.keccak (text=sgn)

  reg = f.env.contracts.registry
  tokenId = reg.functions.tokenIdForName (ns, nm).call ()

  events = f.w3.eth.get_logs ({
    "address": reg.address,
    "topics": [fpr.hex (), "0x%064x" % tokenId],
  })

  moveEvent = reg.events.Move ()
  for ev in events:
    proc = moveEvent.processLog (ev)
    if proc["args"]["nonce"] == nonce:
      return f.w3.keccak (hexstr=ev["data"]).hex ()[2:]

  raise AssertionError ("no move logs found for %s/%s nonce %d"
                          % (ns, nm, nonce))


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
      txid1 = f.sendMove ("p/dömob", {
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
            "txid": txid2,
            "cmd": "admin",
            "mvid": mvid (f, "g", "game", 0),
            "burnt": 0,
            "out": {addr1: 0.12345678},
          },
        ],
        "moves": [
          {
            "txid": txid1,
            "name": "dömob",
            "move": [1, 2, 3],
            "mvid": mvid (f, "p", "dömob", 0),
            "burnt": 0,
            "out": {},
          },
          {
            "txid": txid3,
            "name": "andy",
            "move": {},
            "mvid": mvid (f, "p", "andy", 0),
            "burnt": 0,
            "out": {addr2: 42},
          },
        ],
      })
