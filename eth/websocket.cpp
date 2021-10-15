// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "websocket.hpp"

#include <websocketpp/client.hpp>
#include <websocketpp/close.hpp>
#include <websocketpp/config/asio_client.hpp>

#include <json/json.h>

#include <glog/logging.h>

#include <atomic>
#include <sstream>
#include <thread>

namespace xayax
{

/* ************************************************************************** */

class WebSocketSubscriber::Connection
{

private:

  using Config = websocketpp::config::asio_client;
  using MessagePtr = Config::message_type::ptr;
  using Client = websocketpp::client<Config>;

  /** ID value sent for the subscribe request to new heads.  */
  static constexpr int ID_NEW_HEADS = 1;
  /** ID value sent for the subscribe request to new pendings.  */
  static constexpr int ID_PENDING_TX = 2;

  /** The subscription ID for new heads.  */
  std::string subNewHeads;
  /** The subscription ID for pending transactions.  */
  std::string subPendingTx;

  /** The callbacks to invoke.  */
  Callbacks& cb;

  /** The websocketpp endpoint instance used.  */
  Client endpoint;

  /** Connection handle for the websocket.  */
  websocketpp::connection_hdl hdl;

  /**
   * Set to true when the worker should close the connection.  We then
   * use an interrupt handler to trigger a check and execute the
   * closing as required.
   */
  std::atomic<bool> shouldStop;

  /** The worker thread running the connection loop.  */
  std::unique_ptr<std::thread> worker;

  /**
   * The handler for incoming messages.  These may be the responses for
   * the initial "subscribe" calls, or the actual notifications.
   */
  void HandleMessage (websocketpp::connection_hdl h, MessagePtr msg);

  /**
   * Sends an eth_subscribe method call with the given ID and the
   * given subscription parameter.
   */
  void SendSubscribe (Client::connection_ptr conn,
                      int id, const std::string& type);

public:

  explicit Connection (const std::string& url, Callbacks& c);
  ~Connection ();

  /**
   * Subscribes to pending transactions.
   */
  void EnablePending ();

};

WebSocketSubscriber::Connection::Connection (
    const std::string& url, Callbacks& c)
  : cb(c), shouldStop(false)
{
  try
    {
      endpoint.clear_access_channels (websocketpp::log::alevel::all);
      endpoint.init_asio ();

      endpoint.set_open_handler (
        [this] (const websocketpp::connection_hdl h)
          {
            auto conn = endpoint.get_con_from_hdl (h);
            SendSubscribe (conn, ID_NEW_HEADS, "newHeads");
          });

      endpoint.set_message_handler (
        [this] (const websocketpp::connection_hdl h, const MessagePtr msg)
          {
            HandleMessage (h, msg);
          });

      endpoint.set_interrupt_handler (
        [this] (const websocketpp::connection_hdl h)
          {
            if (shouldStop)
              endpoint.get_con_from_hdl (h)
                  ->close (websocketpp::close::status::normal, "");
          });

      websocketpp::lib::error_code ec;
      auto conn = endpoint.get_connection (url, ec);
      CHECK (!ec) << "WebSocket connection failed: " << ec.message ();

      hdl = conn->get_handle ();
      endpoint.connect (conn);
    }
  catch (const websocketpp::exception& exc)
    {
      LOG (FATAL) << "WebSocket error: " << exc.what ();
    }

  /* Start the networking loop for the websocket client on a new thread.
     This will automatically exit when the connection is closed.  */
  worker = std::make_unique<std::thread> ([this] ()
    {
      try
        {
          endpoint.run ();
        }
      catch (const websocketpp::exception& exc)
        {
          LOG (FATAL) << "WebSocket error: " << exc.what ();
        }
    });
}

WebSocketSubscriber::Connection::~Connection ()
{
  /* There is no specific need to unsubscribe from the notifications,
     as they are tied to the connection anyway.  So by closing the
     connection, the Ethereum node will clear the subscriptions as well.  */

  if (worker != nullptr)
    {
      shouldStop = true;
      endpoint.interrupt (hdl);
      worker->join ();
      worker.reset ();
    }
}

void
WebSocketSubscriber::Connection::EnablePending ()
{
  auto conn = endpoint.get_con_from_hdl (hdl);
  SendSubscribe (conn, ID_PENDING_TX, "newPendingTransactions");
}

void
WebSocketSubscriber::Connection::SendSubscribe (
    const Client::connection_ptr conn,
    const int id, const std::string& type)
{
  Json::Value req(Json::objectValue);

  req["jsonrpc"] = "2.0";
  req["id"] = id;
  req["method"] = "eth_subscribe";

  Json::Value params(Json::arrayValue);
  params.append (type);
  req["params"] = params;

  std::ostringstream out;
  out << req;
  conn->send (out.str ());
}

void
WebSocketSubscriber::Connection::HandleMessage (
    const websocketpp::connection_hdl h, const MessagePtr msg)
{
  std::istringstream in(msg->get_payload ());
  Json::Value data;
  in >> data;

  if (data.isMember ("result"))
    {
      const int id = data["id"].asInt ();
      switch (id)
        {
        case ID_NEW_HEADS:
          subNewHeads = data["result"].asString ();
          LOG (INFO) << "Subscribed to new heads: " << subNewHeads;
          break;

        case ID_PENDING_TX:
          subPendingTx = data["result"].asString ();
          LOG (INFO) << "Subscribed to pending transactions: " << subPendingTx;
          break;

        default:
          LOG (FATAL) << "Unexpected ID received: " << id;
        }
    }

  if (data.isMember ("method")
        && data["method"].asString () ==  "eth_subscription")
    {
      const auto& params = data["params"];
      CHECK (params.isObject ());

      const std::string sub = params["subscription"].asString ();
      const auto& result = params["result"];

      if (sub == subNewHeads)
        {
          CHECK (result.isObject ());
          cb.NewTip (result["hash"].asString ());
        }
      else if (sub == subPendingTx)
        {
          CHECK (result.isString ());
          cb.NewPendingTx (result.asString ());
        }
    }
}

/* ************************************************************************** */

WebSocketSubscriber::WebSocketSubscriber (const std::string& ep)
  : endpoint(ep)
{}

WebSocketSubscriber::~WebSocketSubscriber ()
{
  Stop ();
}

void
WebSocketSubscriber::Start (Callbacks& cb)
{
  connection = std::make_unique<Connection> (endpoint, cb);
}

void
WebSocketSubscriber::EnablePending ()
{
  CHECK (connection != nullptr) << "WebSocketSubscriber is not yet started";
  connection->EnablePending ();
}

void
WebSocketSubscriber::Stop ()
{
  connection.reset ();
}

/* ************************************************************************** */

} // namespace xayax
