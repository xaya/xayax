// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAX_TESTUTILS_HPP
#define XAYAX_TESTUTILS_HPP

#include "basechain.hpp"
#include "blockdata.hpp"
#include "private/chainstate.hpp"

#include <json/json.h>

#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace xayax
{

/**
 * Parses a string as JSON, for use in testing when JSON values are needed.
 */
Json::Value ParseJson (const std::string& str);

/**
 * Sleeps for a short amount of time (but enough to trigger other threads).
 */
void SleepSome ();

/**
 * An implementation of the BaseChain connector that uses an in-memory
 * list of blocks.  It keeps track of the underlying tree structure and
 * can thus handle arbitrary reorgs and attaches of new tips.
 */
class TestBaseChain : public BaseChain
{

private:

  /** Mutex for protecting the member fields.  */
  std::mutex mut;

  /** Chain state with in-memory database for the tree structure.  */
  Chainstate chain;

  /**
   * Map of all block data (including blocks on the main chain, which are not
   * necessarily stored / returned by Chainstate).
   */
  std::map<std::string, BlockData> blocks;

  /** Condition variable that gets notified when the tip changes.  */
  std::condition_variable cvNewTip;

  /** Set to true if the notifier thread should stop.  */
  bool shouldStop;

  /**
   * If started, the thread that waits for new push notifications and
   * invokes the callbacks.  We use a separate thread for this (rather than
   * just invoking them whenever the state is changed) to simulate the situation
   * of a real-world implementation more precisely.
   */
  std::unique_ptr<std::thread> notifier;

  /** We use a simple counter to "generate" block hashes.  */
  unsigned hashCounter = 0;

  /**
   * Constructs a new block hash based on our counter.
   */
  std::string NewBlockHash ();

public:

  TestBaseChain ();
  ~TestBaseChain ();

  /**
   * Constructs a new genesis block with the given starting height.
   */
  BlockData NewGenesis (uint64_t h);

  /**
   * Constructs a new block based on the given parent.
   */
  BlockData NewBlock (const std::string& parent);

  /**
   * Constructs a new block following the current tip.
   */
  BlockData NewBlock ();

  /**
   * Sets the given block as genesis.  Returns the block.
   */
  BlockData SetGenesis (const BlockData& blk);

  /**
   * Sets the given block as new tip, doing all related updates and
   * notifications.  Returns the new tip.
   */
  BlockData SetTip (const BlockData& blk);

  void Start () override;
  std::vector<BlockData> GetBlockRange (uint64_t start,
                                        uint64_t count) override;

};

} // namespace xayax

#endif // XAYAX_TESTUTILS_HPP
