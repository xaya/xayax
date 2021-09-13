#!/usr/bin/env python3

# Copyright (C) 2021 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Tests the support for pending moves (getrawmempool as well as the
ZMQ push notifications).
"""


import coretest
from moves import fillMetadata

from xayax.testcase import ZmqSubscriber

import jsonrpclib


if __name__ == "__main__":
  with coretest.Fixture () as f:
    sub = ZmqSubscriber (f.zmqCtx, f.env.getXRpcUrl (), "game")
    sub.subscribe ("game-pending-move")
    with sub.run ():
      rpc = f.env.createCoreRpc ()
      xrpc = jsonrpclib.ServerProxy (f.env.getXRpcUrl ())

      f.env.register ("x", "foo")
      f.env.register ("p", "domob")
      f.env.register ("p", "andy")
      f.generate (1)
      f.syncBlocks ()

      txid1 = f.sendMove ("x/foo", {"g": {"game": {"foo": "bar"}}})
      txid2 = f.sendMove ("p/domob", {"g": {"game": [1, 2, 3]}})
      txid3 = f.sendMove ("p/domob", {"g": {"game": 42}})

      _, data2 = sub.receive ()
      f.assertEqual (data2, [
        fillMetadata (f, txid2, {
          "name": "domob",
          "move": [1, 2, 3],
        }),
      ])
      _, data3 = sub.receive ()
      f.assertEqual (data3, [
        fillMetadata (f, txid3, {
          "name": "domob",
          "move": 42,
        }),
      ])
      f.assertEqual (xrpc.getrawmempool (), [txid1, txid2, txid3])

      txid4 = f.sendMove ("p/andy", {"g": {"game": {}}})
      _, data4 = sub.receive ()
      f.assertEqual (data4, [
        fillMetadata (f, txid4, {
          "name": "andy",
          "move": {},
        }),
      ])
      f.assertEqual (xrpc.getrawmempool (), [txid1, txid2, txid3, txid4])

      [blk] = f.generate (1)
      f.syncBlocks ()
      f.assertEqual (xrpc.getrawmempool (), [])

      rpc.invalidateblock (blk)
      _, d = sub.receive ()
      f.assertEqual (d, data2)
      _, d = sub.receive ()
      f.assertEqual (d, data3)
      _, d = sub.receive ()
      f.assertEqual (d, data4)

      f.assertEqual (xrpc.getrawmempool (), [txid1, txid2, txid3, txid4])

      # Turn off the check about no unexpected messages.  Pendings and the
      # mempool are "unreliable", so we only want to check that we get
      # notified about the transactions and that the mempool matches
      # the expectations.
      sub.allowExtraMessages = True
