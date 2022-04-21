// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAX_ETH_WEBSOCKET_HPP
#define XAYAX_ETH_WEBSOCKET_HPP

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

  /** The active connection (if any).  */
  std::unique_ptr<Connection> connection;

public:

  class Callbacks;

  explicit WebSocketSubscriber (const std::string& ep);
  ~WebSocketSubscriber ();

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
