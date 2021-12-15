// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAX_TESTUTILS_HPP
#define XAYAX_TESTUTILS_HPP

#include "basechain.hpp"
#include "blockdata.hpp"
#include "private/chainstate.hpp"

#include <json/json.h>
#include <zmq.hpp>

#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
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

  /** The mempool of txids to return.  */
  std::vector<std::string> mempool;

  /** The chain string to return.  */
  std::string chainString = "test";
  /** The version number to return.  */
  uint64_t version = 0;

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

  /**
   * Attaches n blocks starting from the given hash.  Returns the
   * attached blocks.
   */
  std::vector<BlockData> AttachBranch (const std::string& parent, unsigned n);

  /**
   * Adds the given tx as pending.  This calls the PendingMoves notifier,
   * and adds its txid to the mempool.
   */
  void AddPending (const std::vector<MoveData>& moves);

  /**
   * Sets the chain string to return.
   */
  void SetChain (const std::string& str);

  /**
   * Sets the version number to return.
   */
  void SetVersion (uint64_t v);

  void Start () override;
  bool EnablePending () override;
  uint64_t GetTipHeight () override;
  std::vector<BlockData> GetBlockRange (uint64_t start,
                                        uint64_t count) override;
  int64_t GetMainchainHeight (const std::string& hash) override;
  std::vector<std::string> GetMempool () override;
  bool VerifyMessage (const std::string& msg, const std::string& signature,
                      std::string& addr) override;
  std::string GetChain () override;
  uint64_t GetVersion () override;

};

/**
 * ZMQ subscriber that can be connected to a ZmqPub instance for testing
 * the notifications we receive.  It automatically verifies that sequence
 * numbers are correct.
 */
class TestZmqSubscriber
{

private:

  zmq::context_t ctx;
  zmq::socket_t sock;

  /** Mutex for this instance.  */
  std::mutex mut;

  /** Condition variable notified when new messages are received.  */
  std::condition_variable cv;

  /** Expected next sequence number for each command.  */
  std::map<std::string, unsigned> nextSeq;

  /** For each command, the queue of not-yet-expected messages.  */
  std::map<std::string, std::queue<Json::Value>> messages;

  /** Background thread that polls the ZMQ socket and notifies waiters.  */
  std::unique_ptr<std::thread> receiver;

  /** Set to true when the receiver thread should stop.  */
  bool shouldStop;

  /**
   * Worker method that is run on the receiver thread.
   */
  void ReceiveLoop ();

public:

  /**
   * Constructs the subscriber and connects it to a socket at the given address.
   */
  explicit TestZmqSubscriber (const std::string& addr);

  /**
   * Cleans up everything, expecting that no unexpected messages have been
   * received in the mean time.
   */
  ~TestZmqSubscriber ();

  /**
   * Expects num messages to be received with the given topic (waiting until
   * we get them), and returns all associated JSON data.
   */
  std::vector<Json::Value> AwaitMessages (const std::string& cmd, size_t num);

  /**
   * Forgets / ignores all unexpected messages.
   */
  void ForgetAll ();

};

} // namespace xayax

#endif // XAYAX_TESTUTILS_HPP
