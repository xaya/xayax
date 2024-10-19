#!/usr/bin/env python3

# Copyright (C) 2021-2022 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Tests tracking of pending moves and extracting the base data for them.
"""


import ethtest

import moves_data

from xayax.eth import uintToXaya
from xayax.testcase import ZmqSubscriber

import jsonrpclib

import copy
import json


class PendingFixture (ethtest.Fixture):
  """
  Custom test fixture for tests with pending moves.  It adds the multi-mover
  contract to the deployment, and enables pending watching for both the
  basic accounts contract and the multi mover.
  """

  def setupExtraDeployment (self, env):
    env.contracts.multi = self.deployMultiMover (env)
    env.addWatchedContract (env.contracts.registry.address)
    env.addWatchedContract (env.contracts.multi.address)

    # Set up another contract which is not on the watched list.
    env.contracts.extra = self.deployMultiMover (env)


def extractMvids (data):
  """
  Removes the mvid values from all the entries in data, returning
  the modified array.  This allows us to easily compare against the
  expected data, without needing to know the mvid (which is particularly
  cumbersome for pending moves where we can't even query for logs yet).
  """

  res = []
  for d in data:
    modified = copy.deepcopy (d)
    del modified["mvid"]
    res.append (modified)

  return res


if __name__ == "__main__":
  with PendingFixture () as f:
    contracts = f.env.contracts

    f.env.register ("p", "domob")
    f.env.register ("p", "andy")
    f.generate (1)
    f.syncBlocks ()

    sub = ZmqSubscriber (f.zmqCtx, f.env.getXRpcUrl (), "game")
    sub.subscribe ("game-pending-move")
    with sub.run ():
      addr = f.w3.eth.accounts[1]
      xrpc = jsonrpclib.ServerProxy (f.env.getXRpcUrl ())

      # This will not trigger a pending move and should just be
      # handled gracefully.
      f.env.register ("p", "foobar")

      # This is actually a move, but from a contract not tracked and thus
      # won't show up in the pending notifications either.
      contracts.extra.functions.send (["p"], ["domob"], [
        json.dumps ({"g": {"game": "should be ignored"}})
      ]).transact ({"from": contracts.account, "gas": 500_000})

      # The mempool only contains actual tracked transactions.
      f.assertEqual (xrpc.getrawmempool (), [])
      # The second call ensures that the implementation has no problem
      # with an empty internal pool (while the first call could in theory
      # have a non-empty pool to start with and just removed some transactions).
      f.assertEqual (xrpc.getrawmempool (), [])

      # Trigger some pending moves with various specific properties.
      ids = [
        f.sendMove ("p/domob", {"g": {"game": "foo"}}),
        uintToXaya (
          contracts.multi.functions.send (["p"], ["domob", "andy"], [
            json.dumps ({"g": {"game": "bar"}}),
          ]).transact ({"from": contracts.account, "gas": 500_000}).hex ()
        ),
        f.sendMove ("p/andy", {"g": {"game": "with chi"}},
                    send=(addr, 1234_5678)),
        uintToXaya (
          contracts.multi.functions.requireEth (
              "p", "domob",
              json.dumps ({"g": {"game": "with eth"}})
          ).transact ({"from": contracts.account, "value": 10}).hex ()
        ),
      ]

      _, data1 = sub.receive ()
      data1 = extractMvids (data1)
      f.assertEqual (data1, [
        {
          "name": "domob",
          "move": "foo",
          "txid": ids[0],
          "burnt": 0,
          "out": {},
        },
      ])

      _, data2 = sub.receive ()
      data2 = extractMvids (data2)
      f.assertEqual (data2, [
        {
          "name": "domob",
          "move": "bar",
          "txid": ids[1],
          "burnt": 0,
          "out": {},
        },
        {
          "name": "andy",
          "move": "bar",
          "txid": ids[1],
          "burnt": 0,
          "out": {},
        },
      ])

      _, data3 = sub.receive ()
      data3 = extractMvids (data3)
      f.assertEqual (data3, [
        {
          "name": "andy",
          "move": "with chi",
          "txid": ids[2],
          "burnt": 0,
          "out": {addr: 0.12345678},
        },
      ])

      _, data4 = sub.receive ()
      data4 = extractMvids (data4)
      f.assertEqual (data4, [
        {
          "name": "domob",
          "move": "with eth",
          "txid": ids[3],
          "burnt": 0,
          "out": {},
        },
      ])

      f.assertEqual (xrpc.getrawmempool (), ids)

      snapshot = f.env.snapshot ()
      f.generate (1)
      f.syncBlocks ()
      f.assertEqual (xrpc.getrawmempool (), [])

      # To check the mvid field, we do another move now.  A difficulty
      # is that the mvid depends on the nonce, but the nonce from a pending
      # move will only be correct if there are no prior pending moves.  Thus
      # checking all mvid's on the pendings from above would not work out,
      # and doing a separate check specifically for mvid is easier.
      txid = f.sendMove ("p/domob", {"g": {"game": "mvid test"}})
      _, dataMvid = sub.receive ()
      f.generate (1)
      f.syncBlocks ()
      f.assertEqual (dataMvid, [
        {
          "name": "domob",
          "move": "mvid test",
          "mvid": moves_data.mvid (f, "p", "domob", 4),
          "txid": txid,
          "burnt": 0,
          "out": {},
        },
      ])

      # FIXME: It seems Anvil is not resurrecting transactions.
      # Look into this and see what to do for testing reorgs.
      if False:
        snapshot.restore ()
        _, d = sub.receive ()
        f.assertEqual (extractMvid (d), data1)
        _, d = sub.receive ()
        f.assertEqual (extractMvid (d), data2)
        _, d = sub.receive ()
        f.assertEqual (extractMvid (d), data3)
        _, d = sub.receive ()
        f.assertEqual (extractMvid (d), data4)
        _, d = sub.receive ()
        f.assertEqual (extractMvids (d), extractMvids (dataMvid))
        # f.assertEqual (xrpc.getrawmempool (), ids + [txid])
