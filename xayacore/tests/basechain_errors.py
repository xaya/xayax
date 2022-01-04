#!/usr/bin/env python3

# Copyright (C) 2021 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Tests that temporary errors in the base-chain connection are handled
gracefully.
"""


import coretest

from xayax.testcase import ZmqSubscriber

import jsonrpclib

import time


class FastUpdateFixture (coretest.Fixture):
  """
  Extended fixture class that sets a smaller update-timeout value
  for Xaya X so the test runs faster (even if we wait for multiple
  timeouts to occur).
  """

  # The reduced update timeout value we use in ms.
  TIMEOUT_MS = 100

  def getXayaXExtraArgs (self):
    return ["--xayax_update_timeout_ms=%d" % self.TIMEOUT_MS]


def runTest (f, errorFcn):
  """
  Runs one instance of the test in the given fixture.  The errorFcn
  is invoked to temporarily break the RPC proxy, i.e. defines what
  type of error the test simulates.
  """

  sub = ZmqSubscriber (f.zmqCtx, f.env.getXRpcUrl (), "game")
  sub.subscribe ("game-block-attach")
  sub.subscribe ("game-block-detach")

  with sub.run ():
    rpc = f.env.createCoreRpc ()
    xrpc = jsonrpclib.ServerProxy (f.env.getXRpcUrl ())

    base = f.generate (10)
    branch1 = f.generate (10)
    f.waitForZmqTip (sub, branch1[-1])

    # Break the base-chain RPC temporarily.  Then we wait at least for
    # some forced updates to happen (to make sure they are handled gracefully
    # even with errors), update the base chain to a new branch, fix up the
    # RPC and wait for the sync to catch back on.
    f.log.info ("Turning on RPC breakage now")
    errorFcn ()
    rpc.invalidateblock (branch1[0])
    branch2 = f.generate (5)
    time.sleep (2e-3 * f.TIMEOUT_MS)

    # TODO: Also test RPC commands (e.g. game_sendupdates and requesting
    # the chain state directly).

    f.log.info ("Restoring RPC connection")
    f.env.proxy.fixup ()
    f.assertZmqBlocks (sub, "detach", branch1[::-1])
    f.assertZmqBlocks (sub, "attach", branch2)


if __name__ == "__main__":
  with FastUpdateFixture () as f:
    f.mainLogger.info ("Testing disconnected base-chain RPC...")
    runTest (f, f.env.proxy.stop)
  with FastUpdateFixture () as f:
    f.mainLogger.info ("Testing base-chain HTTP errors...")
    runTest (f, f.env.proxy.enableHttpErrors)
  with FastUpdateFixture () as f:
    f.mainLogger.info ("Testing base-chain RPC errors...")
    runTest (f, f.env.proxy.enableRpcErrors)
