// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "testutils.hpp"

#include <glog/logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <condition_variable>
#include <mutex>

namespace xayax
{
namespace
{

using testing::ElementsAre;

/* ************************************************************************** */

/**
 * Fake implementation of BaseChain callbacks that allows waiting for
 * the callbacks to be invoked as expected.
 */
class WaitingCallbacks : public BaseChain::Callbacks
{

private:

  /** Mutex for this instance and the condition variable.  */
  std::mutex mut;

  /** Condition variable notified when something changes.  */
  std::condition_variable cv;

  /** Number of times TipChanged has been invoked.  */
  unsigned tipChanges = 0;

public:

  WaitingCallbacks () = default;

  /**
   * Expects that TipChanged has been invoked exactly the given number of
   * times, resetting the counter.
   */
  void
  ExpectTipChanges (const unsigned expected)
  {
    std::unique_lock<std::mutex> lock(mut);
    while (tipChanges < expected)
      cv.wait (lock);
    EXPECT_EQ (tipChanges, expected) << "Unexpected TipChanged invocations";
    tipChanges = 0;
  }

  void
  TipChanged () override
  {
    std::lock_guard<std::mutex> lock(mut);
    ++tipChanges;
    cv.notify_all ();
  }

  void
  PendingMoves (const std::vector<MoveData>& moves) override
  {
    /* Nothing special happens here.  The pending logic inside the test base
       chain is trivial enough to not require special testing here.  */
  }

};

class TestBaseChainTests : public testing::Test
{

protected:

  WaitingCallbacks cb;
  TestBaseChain bc;

  TestBaseChainTests ()
  {
    bc.SetCallbacks (&cb);
    bc.Start ();

    /* Give the notifier thread some time to actually start.  */
    SleepSome ();
  }

};

TEST_F (TestBaseChainTests, TipNotifications)
{
  const auto genesis = bc.SetGenesis (bc.NewGenesis (10)).hash;
  SleepSome ();

  bc.SetTip (bc.NewBlock ());
  SleepSome ();
  const auto tip = bc.SetTip (bc.NewBlock ()).hash;
  SleepSome ();

  bc.SetTip (bc.NewBlock (genesis));
  SleepSome ();
  bc.SetTip (bc.NewBlock (tip));

  cb.ExpectTipChanges (5);
}

TEST_F (TestBaseChainTests, GetBlockRange)
{
  EXPECT_THAT (bc.GetBlockRange (10, 5), ElementsAre ());

  const auto genesis = bc.SetGenesis (bc.NewGenesis (10));
  const auto a = bc.SetTip (bc.NewBlock ());
  const auto b = bc.SetTip (bc.NewBlock ());

  EXPECT_THAT (bc.GetBlockRange (10, 5), ElementsAre (genesis, a, b));
  EXPECT_THAT (bc.GetBlockRange (10, 2), ElementsAre (genesis, a));
  EXPECT_THAT (bc.GetBlockRange (11, 5), ElementsAre (a, b));

  const auto c = bc.SetTip (bc.NewBlock (a.hash));
  EXPECT_THAT (bc.GetBlockRange (11, 5), ElementsAre (a, c));

  EXPECT_THAT (bc.GetBlockRange (100, 5), ElementsAre ());
  EXPECT_THAT (bc.GetBlockRange (10, 0), ElementsAre ());
  EXPECT_THAT (bc.GetBlockRange (9, 5), ElementsAre ());
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace xayax
