#!/usr/bin/env python3

# Copyright (C) 2021 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Tests block attaches / detaches, including reorgs and custom requests.
"""


import ethtest

from xayax.testcase import ZmqSubscriber

import jsonrpclib

import time


if __name__ == "__main__":
  with ethtest.Fixture () as f:
    sub = ZmqSubscriber (f.zmqCtx, f.env.getXRpcUrl (), "game")
    sub.subscribe ("game-block-attach")
    sub.subscribe ("game-block-detach")
    with sub.run ():
      xrpc = jsonrpclib.ServerProxy (f.env.getXRpcUrl ())
      start = time.monotonic ()

      base = f.generate (10)
      f.assertZmqBlocks (sub, "attach", base)
      snapshot = f.env.snapshot ()

      branch1 = f.generate (10)
      f.assertZmqBlocks (sub, "attach", branch1)

      snapshot.restore ()
      # Make sure we do not regenerate the same branch again.
      f.sendMove ("p/domob", {})
      branch2 = f.generate (5)
      assert branch1[0] != branch2[0]
      f.assertZmqBlocks (sub, "detach", branch1[::-1])
      f.assertZmqBlocks (sub, "attach", branch2)

      data = xrpc.game_sendupdates (gameid="game", fromblock=branch1[-1])
      f.assertEqual (data["steps"], {
        "detach": 10,
        "attach": 5,
      })
      f.assertEqual (data["toblock"], branch2[-1])
      f.assertZmqBlocks (sub, "detach", branch1[::-1],
                         reqtoken=data["reqtoken"])
      f.assertZmqBlocks (sub, "attach", branch2, reqtoken=data["reqtoken"])

      # Make sure that the test was reasonably fast.  In particular, this
      # verifies that we did not rely on the periodic polling for new blocks,
      # and instead got notified about them immediately.
      end = time.monotonic ()
      duration = end - start
      assert duration > 0
      f.mainLogger.info ("Runtime: %.3fs" % duration)
      assert duration < 1, "Test took too long"
