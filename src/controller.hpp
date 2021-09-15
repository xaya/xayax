// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAX_CONTROLLER_HPP
#define XAYAX_CONTROLLER_HPP

#include "basechain.hpp"

#include <condition_variable>
#include <mutex>
#include <set>
#include <string>

namespace xayax
{

/**
 * An instance of Xaya X, which holds a reference to a BaseChain implementation
 * and uses it to keep a local state up-to-date.  It also provides the RPC and
 * ZMQ interface similar to Xaya Core, which GSPs can connect to.
 */
class Controller
{

private:

  class RpcServer;
  class RunData;

  /** The underlying base chain being used.  */
  BaseChain& base;

  /** Folder for the data directory with local state.  */
  const std::string dataDir;

  /** Hash for the genesis block we use.  */
  std::string genesisHash;
  /** Height for our genesis block.  */
  uint64_t genesisHeight;

  /**
   * Set to true if pending tracking is enabled and the base chain supports it.
   */
  bool pending = false;

  /** Games to track upon start.  */
  std::set<std::string> trackedGames;

  /** Whether or not sanity checks are enabled.  */
  bool sanityChecks = false;

  /**
   * If set to something other than -1, pruning of move data in the
   * chain state is enabled for blocks this far behind the tip.
   */
  int pruning = -1;

  /** Endpoint for the ZMQ server.  */
  std::string zmqAddr;

  /** Whether or not the RPC server should listen only locally.  */
  bool rpcListenLocally;
  /** Port for the RPC server.  */
  int rpcPort = -1;

  /** Mutex for this instance (for the Run/Stop interaction).  */
  std::mutex mut;

  /** Condition variable notified when Run should stop.  */
  std::condition_variable cv;

  /** The active run data if it is running.  */
  RunData* run = nullptr;
  /** Set to true when Run should stop.  */
  bool shouldStop;

  /**
   * Disables the sync task in the running controller.  This can be used
   * for some tests, where we want the local chain state and the base chain
   * to deviate.
   */
  void DisableSyncForTesting ();

  friend class ControllerTests;

protected:

  /**
   * Callback that is invoked when a run has been started and the servers
   * set up, but before syncing is activated.  This is useful for tests, as they
   * can then connect the ZMQ subscriber properly without missing the initial
   * messages sent.
   */
  virtual void
  ServersStarted ()
  {}

public:

  /**
   * Constructs a new instance, using the given folder as the data directory
   * for storing local state.  The base chain needs to be already started.
   * The controller will modify the base chain's callbacks.
   */
  explicit Controller (BaseChain& bc, const std::string& dir);

  virtual ~Controller ();

  /**
   * Configures the genesis block we want to use in the local chain state.
   */
  void SetGenesis (const std::string& hash, uint64_t height);

  /**
   * Sets up the endpoint where the ZMQ interface should connect.
   */
  void SetZmqEndpoint (const std::string& addr);

  /**
   * Sets up the binding parameters (port and whether or not to bind only
   * on localhost) for the RPC server.
   */
  void SetRpcBinding (int p, bool local);

  /**
   * Tries to enable tracking of pending moves.  This will call EnablePending
   * on the base-chain implementation, and if the base chain supports pendings,
   * expose the pending ZMQ endpoint from getzmqnotifications.
   */
  void EnablePending ();

  /**
   * Enables internal sanity checks.  This will slow down operation
   * and should mainly be used for testing.
   */
  void EnableSanityChecks ();

  /**
   * Turns on pruning of move data for blocks that are a certain depth
   * behind the tip.
   */
  void EnablePruning (unsigned depth);

  /**
   * Marks a given game to be tracked right upon start of the controller.
   * Must be called before actually starting (but after start, tracking and
   * untracking of games is still possible through the RPC interface).
   */
  void TrackGame (const std::string& gameId);

  /**
   * Signals an active Run call to stop (from another thread).  When this
   * method returns, the Run call may not yet have actually returned, but
   * it will do so shortly.
   */
  void Stop ();

  /**
   * Starts up the instance and RPC/ZMQ servers.  The call to this method
   * blocks until Stop() is called on another thread or via the RPC server.
   */
  void Run ();

};

} // namespace xayax

#endif // XAYAX_CONTROLLER_HPP
