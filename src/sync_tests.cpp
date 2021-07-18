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

  void
  TipUpdatedFrom (const std::string& oldTip) override
  {
    std::lock_guard<std::mutex> lock(mutChain);
    CHECK_EQ (oldTip, currentTip);
    currentTip = GetCurrentTip (chain);
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
  {}

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
  for (unsigned i = 0; i < 10; ++i)
    blk = base.SetTip (base.NewBlock ());
  StartSync (genesis);
  cb.WaitForTip (blk.hash);

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
  BlockData blk;
  for (unsigned i = 0; i < 6; ++i)
    blk = base.SetTip (base.NewBlock ());

  sync->NewBaseChainTip ();
  cb.WaitForTip (blk.hash);

  StopSync ();

  blk = base.SetTip (base.NewBlock (forkPoint.hash));
  for (unsigned i = 0; i < 20; ++i)
    blk = base.SetTip (base.NewBlock ());

  LOG (INFO) << "Restarting sync, doing reorg...";
  StartSync (genesis);
  cb.WaitForTip (blk.hash);
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace xayax
