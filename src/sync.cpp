// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "private/sync.hpp"

#include <glog/logging.h>

#include <chrono>

namespace xayax
{

namespace
{

/** Time between sync updates even if no tip notification is received.  */
constexpr auto UPDATE_TIMEOUT = std::chrono::seconds (5);

/**
 * Time to sleep between update steps even if we are still not fully caught up.
 * This is a small interval just to make sure we are not blocking the lock
 * and processing completely for a long time.
 */
constexpr auto WAIT_BETWEEN_STEPS = std::chrono::milliseconds (1);

/** Maximum number of blocks to request at once during sync.  */
constexpr unsigned MAX_NUM_BLOCKS = 128;

} // anonymous namespace

Sync::Sync (BaseChain& b, Chainstate& c, std::mutex& mutC,
            const std::string& genHash, const uint64_t genHeight)
  : base(b), chain(c), mutChain(mutC),
    genesisHash(genHash), genesisHeight(genHeight)
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
  numBlocks = 2;
  nextStartHeight = -1;

  updater = std::make_unique<std::thread> ([this] ()
    {
      std::unique_lock<std::mutex> lock(mut);
      while (!shouldStop)
        {
          if (UpdateStep ())
            {
              lock.unlock ();
              std::this_thread::sleep_for (WAIT_BETWEEN_STEPS);
              lock.lock ();
            }
          else
            cv.wait_for (lock, UPDATE_TIMEOUT);
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
  cb = c;
}

void
Sync::IncreaseNumBlocks ()
{
  numBlocks = std::min (MAX_NUM_BLOCKS, numBlocks << 1);
}

bool
Sync::RetrieveGenesis ()
{
  const auto blocks = base.GetBlockRange (genesisHeight, 1);
  if (blocks.empty ())
    {
      LOG (WARNING)
          << "Failed to get genesis block at height " << genesisHeight
          << " from the base chain";
      return false;
    }

  CHECK_EQ (blocks.size (), 1);
  const auto& blk = blocks[0];
  CHECK_EQ (blk.hash, genesisHash) << "Mismatch in genesis hash";

  chain.Initialise (blk);
  LOG (INFO)
      << "Retrieved genesis block " << genesisHash << " from the base chain";
  return true;
}

bool
Sync::UpdateStep ()
{
  std::unique_lock<std::mutex> lock(mutChain);

  int64_t startHeight = nextStartHeight;
  if (nextStartHeight == -1)
    {
      const int64_t tipHeight = chain.GetTipHeight ();
      if (tipHeight == -1)
        {
          if (!RetrieveGenesis ())
            return false;

          lock.unlock ();
          if (cb != nullptr)
            cb->TipUpdatedFrom ("");

          return true;
        }
      startHeight = tipHeight + 1;
    }

  CHECK_GE (startHeight, 0);
  VLOG (1)
      << "Requesting " << numBlocks << " blocks from " << startHeight
      << " from the base chain";

  const auto blocks = base.GetBlockRange (startHeight, numBlocks);

  if (blocks.empty ())
    return false;

  const auto& firstBlock = blocks[0];
  std::string oldTip;
  if (!chain.SetTip (firstBlock, oldTip))
    {
      /* The first block does not fit to our existing chain.  We need to
         go back and request blocks prior until we find the fork point.  */
      IncreaseNumBlocks ();
      nextStartHeight = std::max<int64_t> (genesisHeight,
                                           startHeight - numBlocks);
      return true;
    }

  /* If we managed to attach the first block, we are done looking for
     a reorg fork point.  */
  nextStartHeight = -1;

  for (unsigned i = 1; i < blocks.size (); ++i)
    {
      std::string prev;
      CHECK (chain.SetTip (blocks[i], prev));
      CHECK_EQ (prev, blocks[i].parent);
      CHECK_EQ (prev, blocks[i - 1].hash);
    }

  lock.unlock ();
  if (cb != nullptr)
    cb->TipUpdatedFrom (oldTip);

  /* If we received fewer blocks than requested, we are caught up.  */
  if (blocks.size () < numBlocks)
    {
      numBlocks = 2;
      return false;
    }

  /* Otherwise, we continue retrieving blocks.  */
  IncreaseNumBlocks ();
  return true;
}

} // namespace xayax
