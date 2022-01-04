// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "private/sync.hpp"

#include "private/chainstate.hpp"
#include "testutils.hpp"

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace xayax
{

DECLARE_int32 (xayax_update_timeout_ms);

namespace
{

/* ************************************************************************** */

/**
 * Returns the current tip block hash from a chainstate.
 */
std::string
GetCurrentTip (const Chainstate& c)
{
  const int64_t height = c.GetTipHeight ();
  if (height == -1)
    return "";

  CHECK_GE (height, 0);

  std::string res;
  CHECK (c.GetHashForHeight (height, res));

  return res;
}

/**
 * Custom callbacks for Sync, which allow us to wait until the tip is updated
 * to an expected block.  The callbacks also ensure that the "oldTip" passed
 * into the update callback matches the expected value.
 */
class TestCallbacks : public Sync::Callbacks
{

private:

  /** The chainstate we watch.  */
  Chainstate& chain;

  /**
   * Lock for the chain state, which we also use for this instance
   * and the condition variable.
   */
  std::mutex& mutChain;

  /** Current tip as of the last update notification.  */
  std::string currentTip;

  /** Condition variable that gets notified if the tip is updated.  */
  std::condition_variable cv;

  /**
   * Counter for tip-updated calls, so we can make sure there are not any
   * unexpected / spurious ones.
   */
  unsigned updates = 0;

public:

  explicit TestCallbacks (Chainstate& c, std::mutex& mC)
    : chain(c), mutChain(mC)
  {}

  /**
   * Waits until we get updated to the expected tip.
   */
  void
  WaitForTip (const std::string& expected)
  {
    std::unique_lock<std::mutex> lock(mutChain);
    while (expected != GetCurrentTip (chain))
      cv.wait (lock);
  }

  /**
   * Returns the number of tip update calls we have received so far.
   */
  unsigned
  GetNumUpdateCalls () const
  {
    std::lock_guard<std::mutex> lock(mutChain);
    return updates;
  }

  void
  TipUpdatedFrom (const std::string& oldTip,
                  const std::vector<BlockData>& attaches) override
  {
    /* Make sure we handle the situation of a fast-sync correctly, where
       oldTip will be set as "" when we just reimported a new tip.  */
    if (!oldTip.empty ())
      CHECK_EQ (oldTip, currentTip);
    currentTip = GetCurrentTip (chain);

    /* Verify that the block attaches make sense with respect to the
       new chain state.  They should all be on the main chain, ending
       in the current tip, and going back at least to the fork point
       for the branch to the old tip.  */
    CHECK (!attaches.empty ());
    CHECK_EQ (attaches.back ().hash, currentTip);
    for (unsigned i = 1; i < attaches.size (); ++i)
      CHECK_EQ (attaches[i].parent, attaches[i - 1].hash);

    if (!oldTip.empty ())
      {
        std::vector<BlockData> detaches;
        CHECK (chain.GetForkBranch (oldTip, detaches));
        const std::string forkParent
            = (detaches.empty () ? oldTip : detaches.back ().parent);
        bool foundForkPoint = false;
        for (const auto& blk : attaches)
          if (blk.parent == forkParent)
            foundForkPoint = true;
        CHECK (foundForkPoint);
      }

    ++updates;
    cv.notify_all ();
  }

};

class SyncTests : public testing::Test
{

private:

  Chainstate chain;
  std::mutex mutChain;

protected:

  /**
   * If we have an active sync, the instance.  This can be created and
   * destroyed at will during a test, to start and stop syncing.
   */
  std::unique_ptr<Sync> sync;

  TestBaseChain base;
  TestCallbacks cb;

  SyncTests ()
    : chain(":memory:"), cb(chain, mutChain)
  {
    base.Start ();
  }

  /**
   * Starts our sync task, using the given max reorg depth.
   */
  void
  StartSync (const uint64_t pd)
  {
    CHECK (sync == nullptr);
    sync = std::make_unique<Sync> (base, chain, mutChain, pd);
    sync->SetCallbacks (&cb);
    sync->Start ();
  }

  /**
   * Stops the sync task.
   */
  void
  StopSync ()
  {
    CHECK (sync != nullptr);
    sync.reset ();
  }

  /**
   * Exposes the underlying Chainstate that is being synced for arbitrary
   * read operations in tests.
   */
  void
  ReadChainstate (const std::function<void (const Chainstate& c)>& fcn)
  {
    std::lock_guard<std::mutex> lock(mutChain);
    fcn (chain);
  }

};

/* ************************************************************************** */

TEST_F (SyncTests, InitialBlock)
{
  const auto genesis = base.SetGenesis (base.NewGenesis (0));
  const auto branch = base.AttachBranch (genesis.hash, 100);
  StartSync (10);
  cb.WaitForTip (branch.back ().hash);

  ReadChainstate ([] (const Chainstate& chain)
    {
      EXPECT_EQ (chain.GetLowestUnprunedHeight (), 90);
      EXPECT_EQ (chain.GetTipHeight (), 100);
    });
}

TEST_F (SyncTests, BasicSyncing)
{
  /* Make sure we use a long / "real" update time out, so that we can ensure
     it is not actually used.  */
  FLAGS_xayax_update_timeout_ms = 10'000;

  base.SetGenesis (base.NewGenesis (0));
  base.SetTip (base.NewBlock ());
  auto blk = base.SetTip (base.NewBlock ());

  /* The whole syncing from now on should be fast, and in particular, we
     should never fall back into the "long wait" between steps, as we either
     can catch up directly after starting sync, or get notified whenever
     a new tip is available.  */
  using Clock = std::chrono::steady_clock;
  const auto start = Clock::now ();

  StartSync (0);
  cb.WaitForTip (blk.hash);

  for (unsigned i = 0; i < 10; ++i)
    {
      SleepSome ();
      blk = base.SetTip (base.NewBlock ());
      sync->NewBaseChainTip ();
      cb.WaitForTip (blk.hash);
    }

  StopSync ();
  const auto branch = base.AttachBranch (blk.hash, 10);
  /* We don't want fast catchup in this test, it should just sync normally.  */
  StartSync (100);
  cb.WaitForTip (branch.back ().hash);

  const auto end = Clock::now ();
  EXPECT_LT (end - start, std::chrono::seconds (1)) << "Sync should not block";
}

TEST_F (SyncTests, FastCatchup)
{
  const auto genesis = base.SetGenesis (base.NewGenesis (0));
  const auto branch1 = base.AttachBranch (genesis.hash, 5);

  StartSync (5);
  cb.WaitForTip (branch1.back ().hash);
  StopSync ();

  const auto branch2 = base.AttachBranch (genesis.hash, 20);
  StartSync (5);
  cb.WaitForTip (branch2.back ().hash);

  ReadChainstate ([&] (const Chainstate& chain)
    {
      EXPECT_EQ (chain.GetLowestUnprunedHeight (), 15);
      EXPECT_EQ (chain.GetTipHeight (), 20);

      std::vector<BlockData> detaches;
      CHECK (chain.GetForkBranch (branch1.back ().hash, detaches));

      auto expected = branch1;
      std::reverse (expected.begin (), expected.end ());
      EXPECT_EQ (detaches, expected);
    });
}

TEST_F (SyncTests, DiscoversNewBlocks)
{
  /* Use a smaller update timeout to speed up the test.  */
  FLAGS_xayax_update_timeout_ms = 100;

  base.SetGenesis (base.NewGenesis (0));
  StartSync (0);

  SleepSome ();

  const auto blk = base.SetTip (base.NewBlock ());

  /* Even if we do not notify the sync task about a new base chain tip,
     it should eventually discover it by itself (and not block forever).  */
  cb.WaitForTip (blk.hash);
}

TEST_F (SyncTests, LongReorg)
{
  base.SetGenesis (base.NewGenesis (0));
  StartSync (13);

  const auto forkPoint = base.SetTip (base.NewBlock ());

  /* With doubling of the blocks to look back, this length of the reorg
     will lead to a start height below zero at one point.  Thus we test and
     make sure this also works properly.  */
  const auto shortBranch = base.AttachBranch (forkPoint.hash, 13);

  sync->NewBaseChainTip ();
  cb.WaitForTip (shortBranch.back ().hash);

  StopSync ();

  const auto longBranch = base.AttachBranch (forkPoint.hash, 20);

  LOG (INFO) << "Restarting sync, doing reorg...";
  StartSync (100);
  cb.WaitForTip (longBranch.back ().hash);
}

TEST_F (SyncTests, ShortReorg)
{
  /* Even though that is not what happens in practice typically, the
     syncing process should detect if the the chain tip changes also
     if the new tip has a lower block height than the old (local) tip.  */

  const auto genesis = base.SetGenesis (base.NewGenesis (0));
  const auto longBranch = base.AttachBranch (genesis.hash, 10);

  StartSync (10);
  cb.WaitForTip (longBranch.back ().hash);

  const auto reorg = base.SetTip (base.NewBlock (genesis.hash));
  sync->NewBaseChainTip ();
  cb.WaitForTip (reorg.hash);
}

TEST_F (SyncTests, ReactivatingKnownChain)
{
  const auto genesis = base.SetGenesis (base.NewGenesis (0));

  const auto branch1 = base.AttachBranch (genesis.hash, 10);
  StartSync (10);
  cb.WaitForTip (branch1.back ().hash);
  StopSync ();

  const auto branch2 = base.AttachBranch (genesis.hash, 10);
  StartSync (10);
  cb.WaitForTip (branch2.back ().hash);
  StopSync ();

  base.SetTip (branch1.back ());
  StartSync (10);
  cb.WaitForTip (branch1.back ().hash);
}

TEST_F (SyncTests, NoSuperfluousUpdateCalls)
{
  /* Make sure that the update callback is not invoked if the tip
     does not actually change.  */

  base.SetGenesis (base.NewGenesis (0));
  const auto tip = base.SetTip (base.NewBlock ());
  StartSync (0);
  cb.WaitForTip (tip.hash);

  const unsigned calls = cb.GetNumUpdateCalls ();
  sync->NewBaseChainTip ();
  SleepSome ();
  EXPECT_EQ (cb.GetNumUpdateCalls (), calls);
}

TEST_F (SyncTests, VerifiesChainString)
{
  testing::FLAGS_gtest_death_test_style = "threadsafe";

  base.SetChain ("foo");
  const auto genesis = base.SetGenesis (base.NewGenesis (0));
  StartSync (0);
  cb.WaitForTip (genesis.hash);

  StopSync ();
  StartSync (0);
  cb.WaitForTip (genesis.hash);

  StopSync ();
  base.SetChain ("bar");
  EXPECT_DEATH (
    {
      StartSync (0);
      cb.WaitForTip (genesis.hash);
    }, "Chain mismatch");
}

TEST_F (SyncTests, BaseChainErrors)
{
  base.SetGenesis (base.NewGenesis (0));
  const auto blk1 = base.SetTip (base.NewBlock ());

  StartSync (0);
  cb.WaitForTip (blk1.hash);

  base.SetShouldThrow (true);
  const auto blk2 = base.SetTip (base.NewBlock ());

  /* The sync should not yet update (as it can't).  */
  sync->NewBaseChainTip ();
  SleepSome ();
  ReadChainstate ([&] (const Chainstate& chain)
    {
      EXPECT_EQ (GetCurrentTip (chain), blk1.hash);
    });

  /* Now it should recover.  */
  base.SetShouldThrow (false);
  sync->NewBaseChainTip ();
  cb.WaitForTip (blk2.hash);
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace xayax
