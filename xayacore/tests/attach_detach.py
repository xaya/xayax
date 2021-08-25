#!/usr/bin/env python3

# Copyright (C) 2021 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Tests block attaches / detaches, including reorgs and custom requests.
"""


import coretest

from xayax.testcase import ZmqSubscriber

import jsonrpclib

import time


def expectBlocks (f, sub, typ, hashes, reqtoken=None):
  """
  Receives messages on the ZMQ subscriber, expecting attach/detach messages
  (according to typ) for the given block hashes in order.
  """

  for h in hashes:
    topic, data = sub.receive ()
    f.assertEqual (topic, "game-block-%s" % typ)
    f.assertEqual (data["block"]["hash"], h)
    if reqtoken is None:
      f.assertEqual ("reqtoken" in data, False)
    else:
      f.assertEqual (data["reqtoken"], reqtoken)


if __name__ == "__main__":
  with coretest.Fixture () as f:
    sub = ZmqSubscriber (f.zmqCtx, f.env.getXRpcUrl (), "game")
    sub.subscribe ("game-block-attach")
    sub.subscribe ("game-block-detach")
    with sub.run ():
      rpc = f.env.createCoreRpc ()
      xrpc = jsonrpclib.ServerProxy (f.env.getXRpcUrl ())
      start = time.monotonic ()

      base = f.generate (10)
      expectBlocks (f, sub, "attach", base)

      branch1 = f.generate (10)
      expectBlocks (f, sub, "attach", branch1)

      rpc.invalidateblock (branch1[0])
      branch2 = f.generate (5)
      expectBlocks (f, sub, "detach", branch1[::-1])
      expectBlocks (f, sub, "attach", branch2)

      rpc.reconsiderblock (branch1[0])
      expectBlocks (f, sub, "detach", branch2[::-1])
      expectBlocks (f, sub, "attach", branch1)

      data = xrpc.game_sendupdates (gameid="game", fromblock=branch2[-1])
      f.assertEqual (data["steps"], {
        "detach": 5,
        "attach": 10,
      })
      f.assertEqual (data["toblock"], branch1[-1])
      expectBlocks (f, sub, "detach", branch2[::-1], reqtoken=data["reqtoken"])
      expectBlocks (f, sub, "attach", branch1, reqtoken=data["reqtoken"])

      # Make sure that the test was reasonably fast.  In particular, this
      # verifies that we did not rely on the periodic polling for new blocks,
      # and instead got notified about them by ZMQ.
      end = time.monotonic ()
      duration = end - start
      assert duration > 0
      f.mainLogger.info ("Runtime: %.3fs" % duration)
      assert duration < 1, "Test took too long"
