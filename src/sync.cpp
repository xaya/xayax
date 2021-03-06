// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "private/sync.hpp"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <algorithm>
#include <chrono>

namespace xayax
{

DEFINE_int32 (xayax_block_range, 128,
              "maximum number of blocks to process at once");
DEFINE_int32 (xayax_update_timeout_ms, 5'000,
              "time in ms between forced sync updates");

namespace
{

/**
 * Time to sleep between update steps even if we are still not fully caught up.
 * This is a small interval just to make sure we are not blocking the lock
 * and processing completely for a long time.
 */
constexpr auto WAIT_BETWEEN_STEPS = std::chrono::milliseconds (1);

} // anonymous namespace

Sync::Sync (BaseChain& b, Chainstate& c, std::mutex& mutC, const uint64_t pd)
  : base(b), chain(c), mutChain(mutC), pruningDepth(pd)
{}

Sync::~Sync ()
{
  mut.lock ();
  if (updater != nullptr)
    {
      shouldStop = true;
      cv.notify_all ();
      mut.unlock ();
      updater->join ();
      mut.lock ();
      updater.reset ();
    }
  mut.unlock ();
}

void
Sync::Start ()
{
  std::lock_guard<std::mutex> lock(mut);
  CHECK (updater == nullptr);

  shouldStop = false;
  numBlocks = 1;
  nextStartHeight = -1;

  try
    {
      std::lock_guard<std::mutex> lockChain(mutChain);
      chain.SetChain (base.GetChain ());
    }
  catch (const std::exception& exc)
    {
      /* Unlike exceptions thrown by the base chain in the rest of
         the sync code, it is not easily possible to just ignore them
         here in the initialisation.  So in this case, fail.  This is
         something that will be noticed upon startup, though, and not
         a cause of sudden crashes while Xaya X is running.  */
      LOG (FATAL) << "Failed to get connected chain: " << exc.what ();
    }

  updater = std::make_unique<std::thread> ([this] ()
    {
      std::unique_lock<std::mutex> lock(mut);
      const auto timeout
          = std::chrono::milliseconds (FLAGS_xayax_update_timeout_ms);
      while (!shouldStop)
        {
          bool moreSteps;
          try
            {
              moreSteps = UpdateStep ();
            }
          catch (const std::exception& exc)
            {
              LOG (WARNING) << "Error in sync update step: " << exc.what ();
              moreSteps = false;
            }

          if (moreSteps)
            {
              lock.unlock ();
              std::this_thread::sleep_for (WAIT_BETWEEN_STEPS);
              lock.lock ();
            }
          else
            cv.wait_for (lock, timeout);
        }
    });
}

void
Sync::NewBaseChainTip ()
{
  std::lock_guard<std::mutex> lock(mut);
  cv.notify_all ();
}

void
Sync::SetCallbacks (Callbacks* c)
{
  std::lock_guard<std::mutex> lock(mut);
  cb = c;
}

void
Sync::IncreaseNumBlocks ()
{
  CHECK_GE (FLAGS_xayax_block_range, 1) << "Invalid --xayax_block_range set";
  numBlocks = std::min<unsigned> (FLAGS_xayax_block_range, numBlocks << 1);
}

bool
Sync::ImportNewTip (const uint64_t height)
{
  const auto blocks = base.GetBlockRange (height, 1);
  if (blocks.empty ())
    {
      LOG (WARNING)
          << "Failed to get block at height " << height
          << " from the base chain";
      return false;
    }

  CHECK_EQ (blocks.size (), 1);
  const auto& blk = blocks.front ();

  chain.ImportTip (blk);
  LOG (INFO) << "Imported new tip " << blk.hash << " from the base chain";

  if (cb != nullptr)
    cb->TipUpdatedFrom ("", blocks);

  return true;
}

bool
Sync::UpdateStep ()
{
  std::lock_guard<std::mutex> lock(mutChain);

  /* Check the current height of the base chain, and what height we
     want to quick-sync to / initialise at based on the pruning depth.  */
  const uint64_t baseTip = base.GetTipHeight ();
  const uint64_t genesisHeight
      = (baseTip < pruningDepth ? 0 : baseTip - pruningDepth);

  int64_t startHeight = nextStartHeight;
  if (nextStartHeight == -1)
    {
      const int64_t tipHeight = chain.GetTipHeight ();
      if (tipHeight == -1)
        return ImportNewTip (genesisHeight);
      startHeight = tipHeight;
    }

  /* We query for at least three blocks, starting from the current tip.
     This means that normally, the current tip will be returned as first
     block; but if it is not, we can detect that something changed, even
     if the new tip has a lower height.  If a new block has been
     attached (common case), we will receive two blocks, and can then also
     detect immediately that no more blocks are there.  If we get three
     blocks, we will continue querying for more after attaching them.  */
  const unsigned num = std::max<unsigned> (numBlocks, 3);
  CHECK_GE (startHeight, 0);
  VLOG (1)
      << "Requesting " << num << " blocks from " << startHeight
      << " from the base chain";
  const auto blocks = base.GetBlockRange (startHeight, num);

  /* If we are reactivating a chain that we already have locally by
     attaching one of the blocks in that current fork, we need to query
     the corresponding fork branch to get the attach blocks for the
     call to TipUpdatedFrom that precede the blocks we have queried now
     from the base chain.  */
  std::vector<BlockData> oldForkBranch;
  if (!blocks.empty ())
    chain.GetForkBranch (blocks.front ().parent, oldForkBranch);

  std::string oldTip;
  if (blocks.empty () || !chain.SetTip (blocks.front (), oldTip))
    {
      /* The first block does not fit to our existing chain.  We need to
         go back and request blocks prior until we find the fork point.

         Make sure that the first block can (by height) fit to our pruned chain.
         Note that usually we would need the next block to be one *above* the
         lowest unpruned height to fit (so we know the parent as well), but
         there is no harm in requesting that block itself as well.  If it
         matches the one we have, then the attach will be fine.  This also
         covers the case of just detaches back to the lowest unpruned block.  */
      IncreaseNumBlocks ();
      nextStartHeight = std::max<int64_t> (chain.GetLowestUnprunedHeight (),
                                           startHeight - num);
      /* If this did not decrease the start height at all, it means that we are
         already at the lowest unpruned height but that is not good enough.
         In other words, a reorg beyond the pruning depth.  */
      CHECK_LT (nextStartHeight, startHeight) << "Reorg beyond pruning depth";
      return true;
    }

  /* If we managed to attach the first block, we are done looking for
     a reorg fork point.  */
  nextStartHeight = -1;

  /* Attach the actual blocks.  We batch this update in the database,
     so that we avoid many unnecessary disk writes while we are still
     catching up in large chunks.  */
  Chainstate::UpdateBatch upd(chain);
  for (unsigned i = 1; i < blocks.size (); ++i)
    {
      std::string prev;
      CHECK (chain.SetTip (blocks[i], prev));
      CHECK_EQ (prev, blocks[i].parent);
      CHECK_EQ (prev, blocks[i - 1].hash);
    }
  upd.Commit ();

  /* Only notify about a new tip if we actually have a new tip.  This makes
     sure we are not notifying for the case that only the current tip was
     returned in our query.  */
  if (cb != nullptr && oldTip != blocks.back ().hash)
    {
      std::reverse (oldForkBranch.begin (), oldForkBranch.end ());
      for (const auto& b : blocks)
        oldForkBranch.push_back (b);
      cb->TipUpdatedFrom (oldTip, oldForkBranch);
    }

  /* If we received fewer blocks than requested, we are caught up.  */
  if (blocks.size () < num)
    {
      numBlocks = 1;
      return false;
    }

  /* We are now guaranteed to be on the main chain per the base-chain query.
     If we are far behind the base-chain tip (more than the pruning height),
     quick-sync forward by just reimporting the new tip.  Assuming that no
     reorgs happen beyond the pruning depth, this is safe to do and will still
     ensure that all branches a GSP might be attached to are kept.  */
  if (blocks.back ().height < genesisHeight && ImportNewTip (genesisHeight))
    return true;

  /* Otherwise, we continue retrieving blocks.  */
  IncreaseNumBlocks ();
  return true;
}

} // namespace xayax
