#!/usr/bin/env python3

# Copyright (C) 2021 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Tests move/admin data in notifications.
"""


import coretest

from xayax.testcase import ZmqSubscriber

import copy


def fillMetadata (f, txid, extra):
  """
  Retrieves a transaction by id and constructs a dict that contains
  the given "extra" fields plus all the basic metadata (like inputs
  and outputs) as retrieved from Xaya Core.
  """

  rpc = f.env.createCoreRpc ()
  tx = rpc.gettransaction (txid)["hex"]
  data = rpc.decoderawtransaction (tx)

  res = copy.deepcopy (extra)
  res["txid"] = txid
  res["btxid"] = data["btxid"]
  res["inputs"] = [
    {"txid": i["txid"], "vout": i["vout"]}
    for i in data["vin"]
  ]

  outs = {}
  burn = 0
  for o in data["vout"]:
    spk = o["scriptPubKey"]
    if "nameOp" in spk:
      continue
    if "burn" in spk and spk["burn"] == b"g/game".hex ():
      burn += o["value"]
    if "address" in spk:
      if spk["address"] not in outs:
        outs[spk["address"]] = 0.0
      outs[spk["address"]] += o["value"]
  res["out"] = outs
  res["burnt"] = burn

  return res


if __name__ == "__main__":
  with coretest.Fixture () as f:
    sub = ZmqSubscriber (f.zmqCtx, f.env.getXRpcUrl (), "game")
    sub.subscribe ("game-block-attach")
    with sub.run ():
      rpc = f.env.createCoreRpc ()
      addr1 = rpc.getnewaddress ("", "legacy")
      addr2 = rpc.getnewaddress ("", "p2sh-segwit")
      addr3 = rpc.getnewaddress ("", "bech32")

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
      }, options={
        "sendCoins": {
          addr1: 1,
          addr2: 2,
          addr3: 3,
        },
      })
      f.sendMove ("g/abc", {"cmd": 42})
      txid2 = f.sendMove ("g/game", {"cmd": "admin"})
      txid3 = f.sendMove ("p/andy", {
        "g": {"game": {}},
      }, options={
        "burn": {
          "g/game": 10,
          "g/other": 2,
          "game": 3,
        },
      })

      f.generate (1)
      _, data = sub.receive ()
      del data["block"]

      f.assertEqual (data, {
        "admin": [
          fillMetadata (f, txid2, {"cmd": "admin"}),
        ],
        "moves": [
          fillMetadata (f, txid1, {
            "name": "domob",
            "move": [1, 2, 3],
          }),
          fillMetadata (f, txid3, {
            "name": "andy",
            "move": {},
          }),
        ],
      })
