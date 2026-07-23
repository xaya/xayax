// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAX_ETH_WEBSOCKET_HPP
#define XAYAX_ETH_WEBSOCKET_HPP

#include <chrono>
#include <memory>
#include <string>

namespace xayax
{

/**
 * Simple WebSocket client that can subscribe to updates from an Ethereum
 * endpoint and handle push notifications (e.g. for new tips).  This is
 * not possible to do with JSON-RPC over HTTP.
 */
class WebSocketSubscriber
{

private:

  class Connection;

  /** The endpoint to connect to.  */
  const std::string endpoint;

  /**
   * If no message arrives on an open connection for this long, the
   * watchdog assumes it is half-open and tears it down for a reconnect.
   * Zero (the default) disables staleness checking: a sensible timeout
   * depends on the underlying chain's block cadence, so the caller has
   * to opt in with a value that matches it.
   */
  std::chrono::milliseconds stalenessTimeout{0};

  /** The active connection (if any).  */
  std::unique_ptr<Connection> connection;

public:

  class Callbacks;

  explicit WebSocketSubscriber (const std::string& ep);
  ~WebSocketSubscriber ();

  /**
   * Sets the staleness timeout after which a silent but open connection
   * is torn down and reconnected.  Zero disables the check (the default).
   * Must be called before Start.
   */
  void SetStalenessTimeout (std::chrono::milliseconds timeout);

  /**
   * Opens the connection and starts the listening thread.  When notifications
   * are received, the callbacks are invoked accordingly.
   */
  void Start (Callbacks& cb);

  /**
   * Adds a subscription for pending moves to the already running listener.
   */
  void EnablePending ();

  /**
   * Closes the current connection and shuts the listening down.
   */
  void Stop ();

};

/**
 * Callback methods invoked by a websocket subscriber.
 */
class WebSocketSubscriber::Callbacks
{

public:

  virtual ~Callbacks () = default;

  /**
   * Invoked when a new chain tip is found.
   */
  virtual void
  NewTip (const std::string& tip)
  {}

  /**
   * Invoked when a new pending transaction is found.
   */
  virtual void
  NewPendingTx (const std::string& txid)
  {}

};

} // namespace xayax

#endif // XAYAX_ETH_WEBSOCKET_HPP
