// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "private/sync.hpp"

#include "private/chainstate.hpp"
#include "testutils.hpp"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace xayax
{
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
    std::lock_guard<std::mutex> lock(mutChain);

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
   * Starts our sync task, using the given block as genesis.
   */
  void
  StartSync (const BlockData& gen)
  {
    CHECK (sync == nullptr);
    sync = std::make_unique<Sync> (base, chain, mutChain, gen.hash, gen.height);
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

};

/* ************************************************************************** */

TEST_F (SyncTests, WaitingForGenesis)
{
  BlockData blk;
  blk.hash = "genesis";
  blk.height = 2;
  StartSync (blk);

  base.SetGenesis (base.NewGenesis (1));

  SleepSome ();

  blk = base.NewBlock ();
  blk.hash = "genesis";
  base.SetTip (blk);
  sync->NewBaseChainTip ();

  cb.WaitForTip ("genesis");
}

TEST_F (SyncTests, BasicSyncing)
{
  const auto genesis = base.SetGenesis (base.NewGenesis (10));
  base.SetTip (base.NewBlock ());
  auto blk = base.SetTip (base.NewBlock ());

  /* The whole syncing from now on should be fast, and in particular, we
     should never fall back into the "long wait" between steps, as we either
     can catch up directly after starting sync, or get notified whenever
     a new tip is available.  */
  using Clock = std::chrono::steady_clock;
  const auto start = Clock::now ();

  StartSync (genesis);
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
  StartSync (genesis);
  cb.WaitForTip (branch.back ().hash);

  const auto end = Clock::now ();
  EXPECT_LT (end - start, std::chrono::seconds (1)) << "Sync should not block";
}

TEST_F (SyncTests, DiscoversNewBlocks)
{
  auto blk = base.SetGenesis (base.NewGenesis (0));
  StartSync (blk);

  SleepSome ();

  blk = base.SetTip (base.NewBlock ());

  /* Even if we do not notify the sync task about a new base chain tip,
     it should eventually discover it by itself (and not block forever).  */
  cb.WaitForTip (blk.hash);
}

TEST_F (SyncTests, LongReorg)
{
  const auto genesis = base.SetGenesis (base.NewGenesis (0));
  StartSync (genesis);

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
  StartSync (genesis);
  cb.WaitForTip (longBranch.back ().hash);
}

TEST_F (SyncTests, ShortReorg)
{
  /* Even though that is not what happens in practice typically, the
     syncing process should detect if the the chain tip changes also
     if the new tip has a lower block height than the old (local) tip.  */

  const auto genesis = base.SetGenesis (base.NewGenesis (0));
  const auto longBranch = base.AttachBranch (genesis.hash, 10);

  StartSync (genesis);
  cb.WaitForTip (longBranch.back ().hash);

  const auto reorg = base.SetTip (base.NewBlock (genesis.hash));
  sync->NewBaseChainTip ();
  cb.WaitForTip (reorg.hash);
}

TEST_F (SyncTests, NoSuperfluousUpdateCalls)
{
  /* Make sure that the update callback is not invoked if the tip
     does not actually change.  */

  const auto genesis = base.SetGenesis (base.NewGenesis (0));
  const auto tip = base.SetTip (base.NewBlock ());
  StartSync (genesis);
  cb.WaitForTip (tip.hash);

  const unsigned calls = cb.GetNumUpdateCalls ();
  sync->NewBaseChainTip ();
  SleepSome ();
  EXPECT_EQ (cb.GetNumUpdateCalls (), calls);
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace xayax