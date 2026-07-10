// Copyright (C) 2021-2026 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "websocket.hpp"

#include <websocketpp/client.hpp>
#include <websocketpp/close.hpp>
#include <websocketpp/config/asio_client.hpp>

#include <json/json.h>

#include <glog/logging.h>

#include <boost/version.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <thread>

namespace xayax
{

namespace
{

/** ID value sent for the subscribe request to new heads.  */
constexpr int ID_NEW_HEADS = 1;
/** ID value sent for the subscribe request to new pendings.  */
constexpr int ID_PENDING_TX = 2;

/** Initial delay before reconnecting after a failure.  */
constexpr auto BACKOFF_INITIAL = std::chrono::seconds (1);
/** Maximum reconnect delay.  */
constexpr auto BACKOFF_MAX = std::chrono::seconds (30);
/** A session that stayed up at least this long resets the backoff.  */
constexpr auto BACKOFF_RESET_AFTER = std::chrono::seconds (60);

/**
 * If no message (including subscription notifications) arrives for this
 * long, the connection is assumed half-open (e.g. a silently dropped proxy
 * or load-balancer path) and is torn down for a reconnect.  New heads arrive
 * every few seconds on active chains, so this is very conservative.
 */
constexpr auto STALE_TIMEOUT = std::chrono::minutes (3);
/** How often the watchdog checks for staleness.  */
constexpr auto WATCHDOG_INTERVAL = std::chrono::seconds (30);

using PlainClient = websocketpp::client<websocketpp::config::asio_client>;
using TlsClient = websocketpp::client<websocketpp::config::asio_tls_client>;

/**
 * Extracts the host part from a ws:// or wss:// URL, for TLS certificate
 * hostname verification.
 */
std::string
ExtractHost (const std::string& url)
{
  const auto schemeEnd = url.find ("://");
  if (schemeEnd == std::string::npos)
    return "";
  const auto start = schemeEnd + 3;
  const auto end = url.find_first_of (":/", start);
  if (end == std::string::npos)
    return url.substr (start);
  return url.substr (start, end - start);
}

/**
 * Configures TLS on clients supporting it.  The plain overload is a no-op,
 * so the shared session template can call this unconditionally.
 */
void
SetupTlsHandler (PlainClient& c, const std::string& host)
{}

void
SetupTlsHandler (TlsClient& c, const std::string& host)
{
  namespace ssl = boost::asio::ssl;
  c.set_tls_init_handler (
    [host] (const websocketpp::connection_hdl)
      {
        auto ctx = websocketpp::lib::make_shared<ssl::context> (
            ssl::context::tls_client);
        ctx->set_options (ssl::context::default_workarounds
                            | ssl::context::no_sslv2
                            | ssl::context::no_sslv3
                            | ssl::context::no_tlsv1
                            | ssl::context::no_tlsv1_1);
        ctx->set_default_verify_paths ();
        ctx->set_verify_mode (ssl::verify_peer);
#if BOOST_VERSION >= 107300
        ctx->set_verify_callback (ssl::host_name_verification (host));
#else
        ctx->set_verify_callback (ssl::rfc2818_verification (host));
#endif
        return ctx;
      });
}

/**
 * One attempt at a websocket connection.  Runs the networking loop in the
 * calling thread until the connection closes or fails; never throws and
 * never aborts the process.  Reconnection policy lives in the supervisor.
 */
class SessionBase
{

protected:

  SessionBase () = default;

public:

  virtual ~SessionBase () = default;

  /** Runs the connection (blocking) until it ends one way or another.  */
  virtual void Run () = 0;

  /** Requests an orderly stop from another thread.  */
  virtual void RequestStop () = 0;

  /** Subscribes to pending transactions on the live connection.  */
  virtual void EnablePending () = 0;

  /** Milliseconds since the last message was received (or the open).  */
  virtual std::int64_t MessageAgeMs () const = 0;

  /** Whether the websocket handshake ever completed.  */
  virtual bool WasOpened () const = 0;

};

template <typename Config>
class Session : public SessionBase
{

private:

  using Client = websocketpp::client<Config>;
  using MessagePtr = typename Config::message_type::ptr;

  const std::string url;

  WebSocketSubscriber::Callbacks& cb;

  /** Whether pending-tx notifications should be subscribed on open.  */
  const std::atomic<bool>& pendingWanted;

  /** The subscription ID for new heads.  */
  std::string subNewHeads;
  /** The subscription ID for pending transactions.  */
  std::string subPendingTx;

  Client endpoint;
  websocketpp::connection_hdl hdl;

  std::atomic<bool> shouldStop{false};
  std::atomic<bool> opened{false};

  /** Monotonic timestamp (ms) of the last received message or open.  */
  std::atomic<std::int64_t> lastMessage;

  static std::int64_t
  NowMs ()
  {
    return std::chrono::duration_cast<std::chrono::milliseconds> (
        std::chrono::steady_clock::now ().time_since_epoch ()).count ();
  }

  void
  SendSubscribe (typename Client::connection_ptr conn,
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

    const auto ec
        = conn->send (out.str (), websocketpp::frame::opcode::text);
    if (ec)
      LOG (WARNING) << "WebSocket subscribe send failed: " << ec.message ();
  }

  void
  HandleMessage (const MessagePtr msg)
  {
    lastMessage = NowMs ();

    Json::Value data;
    try
      {
        std::istringstream in(msg->get_payload ());
        in >> data;
      }
    catch (const std::exception& exc)
      {
        LOG (WARNING) << "Ignoring unparsable WebSocket message: "
                      << exc.what ();
        return;
      }
    if (!data.isObject ())
      {
        LOG (WARNING) << "Ignoring non-object WebSocket message";
        return;
      }

    if (data.isMember ("error"))
      {
        LOG (ERROR) << "WebSocket subscription error: " << data["error"];
        return;
      }

    if (data.isMember ("result") && data.isMember ("id"))
      {
        switch (data["id"].asInt ())
          {
          case ID_NEW_HEADS:
            subNewHeads = data["result"].asString ();
            LOG (INFO) << "Subscribed to new heads: " << subNewHeads;
            break;

          case ID_PENDING_TX:
            subPendingTx = data["result"].asString ();
            LOG (INFO)
                << "Subscribed to pending transactions: " << subPendingTx;
            break;

          default:
            LOG (WARNING)
                << "Unexpected subscription response ID: " << data["id"];
          }
      }

    if (data.isMember ("method")
          && data["method"].asString () == "eth_subscription")
      {
        const auto& params = data["params"];
        if (!params.isObject ())
          {
            LOG (WARNING) << "Malformed eth_subscription notification";
            return;
          }

        const std::string sub = params["subscription"].asString ();
        const auto& result = params["result"];

        if (sub == subNewHeads && result.isObject ())
          cb.NewTip (result["hash"].asString ());
        else if (sub == subPendingTx && result.isString ())
          cb.NewPendingTx (result.asString ());
      }
  }

public:

  explicit Session (const std::string& u, WebSocketSubscriber::Callbacks& c,
                    const std::atomic<bool>& pending)
    : url(u), cb(c), pendingWanted(pending), lastMessage(NowMs ())
  {}

  void
  Run () override
  {
    try
      {
        endpoint.clear_access_channels (websocketpp::log::alevel::all);
        endpoint.clear_error_channels (websocketpp::log::elevel::all);
        endpoint.init_asio ();

        SetupTlsHandler (endpoint, ExtractHost (url));

        endpoint.set_open_handler (
          [this] (const websocketpp::connection_hdl h)
            {
              opened = true;
              lastMessage = NowMs ();
              LOG (INFO) << "WebSocket connection established: " << url;
              auto conn = endpoint.get_con_from_hdl (h);
              SendSubscribe (conn, ID_NEW_HEADS, "newHeads");
              if (pendingWanted)
                SendSubscribe (conn, ID_PENDING_TX,
                               "newPendingTransactions");
            });

        endpoint.set_message_handler (
          [this] (const websocketpp::connection_hdl h, const MessagePtr msg)
            {
              HandleMessage (msg);
            });

        endpoint.set_fail_handler (
          [this] (const websocketpp::connection_hdl h)
            {
              auto conn = endpoint.get_con_from_hdl (h);
              LOG (WARNING) << "WebSocket connection to " << url
                            << " failed: " << conn->get_ec ().message ();
            });

        endpoint.set_close_handler (
          [this] (const websocketpp::connection_hdl h)
            {
              LOG_IF (WARNING, !shouldStop)
                  << "WebSocket connection to " << url << " closed";
            });

        endpoint.set_interrupt_handler (
          [this] (const websocketpp::connection_hdl h)
            {
              if (shouldStop)
                {
                  websocketpp::lib::error_code ec;
                  endpoint.get_con_from_hdl (h)
                      ->close (websocketpp::close::status::normal, "", ec);
                }
            });

        websocketpp::lib::error_code ec;
        auto conn = endpoint.get_connection (url, ec);
        if (ec)
          {
            LOG (ERROR) << "Invalid WebSocket endpoint " << url << ": "
                        << ec.message ();
            return;
          }

        hdl = conn->get_handle ();
        endpoint.connect (conn);

        /* Runs until the connection is closed or fails; with no pending
           handlers left, this returns and the supervisor decides whether
           to reconnect.  */
        endpoint.run ();
      }
    catch (const std::exception& exc)
      {
        LOG (ERROR) << "WebSocket error for " << url << ": " << exc.what ();
      }
  }

  void
  RequestStop () override
  {
    shouldStop = true;
    try
      {
        endpoint.interrupt (hdl);
      }
    catch (const std::exception& exc)
      {
        /* The connection may already be gone; run() will return.  */
        LOG (WARNING) << "WebSocket interrupt failed: " << exc.what ();
      }
  }

  void
  EnablePending () override
  {
    try
      {
        auto conn = endpoint.get_con_from_hdl (hdl);
        SendSubscribe (conn, ID_PENDING_TX, "newPendingTransactions");
      }
    catch (const std::exception& exc)
      {
        /* pendingWanted is set, so the subscription will be established
           on the next (re)connect anyway.  */
        LOG (WARNING) << "Deferring pending subscription: " << exc.what ();
      }
  }

  std::int64_t
  MessageAgeMs () const override
  {
    return NowMs () - lastMessage;
  }

  bool
  WasOpened () const override
  {
    return opened;
  }

};

} // anonymous namespace

/* ************************************************************************** */

/**
 * Supervisor for websocket sessions.  Owns a worker thread that runs one
 * session after another with exponential backoff, so a failed endpoint,
 * a dropped connection, or a half-open proxy path never kills the process
 * and always recovers.  While no session is connected, the caller's normal
 * polling (the sync loop's timeout) covers block detection.
 */
class WebSocketSubscriber::Connection
{

private:

  const std::string url;
  Callbacks& cb;

  std::atomic<bool> shouldStop{false};
  std::atomic<bool> pendingWanted{false};

  /** The currently running session (if any).  */
  std::shared_ptr<SessionBase> session;
  /** Lock for session (swapped by the supervisor thread).  */
  mutable std::mutex mutSession;

  /** Condition variable for interruptible sleeps.  */
  std::condition_variable cv;
  std::mutex mutCv;

  std::unique_ptr<std::thread> supervisor;
  std::unique_ptr<std::thread> watchdog;

  std::shared_ptr<SessionBase>
  GetSession () const
  {
    std::lock_guard<std::mutex> lock(mutSession);
    return session;
  }

  /** Sleeps for the given duration, waking early on stop.  */
  void
  InterruptibleSleep (const std::chrono::milliseconds duration)
  {
    std::unique_lock<std::mutex> lock(mutCv);
    cv.wait_for (lock, duration, [this] () { return shouldStop.load (); });
  }

  std::shared_ptr<SessionBase>
  MakeSession () const
  {
    if (url.rfind ("wss://", 0) == 0)
      return std::make_shared<Session<websocketpp::config::asio_tls_client>> (
          url, cb, pendingWanted);
    return std::make_shared<Session<websocketpp::config::asio_client>> (
        url, cb, pendingWanted);
  }

  void
  SupervisorLoop ()
  {
    auto backoff = BACKOFF_INITIAL;
    while (!shouldStop)
      {
        auto s = MakeSession ();
        {
          std::lock_guard<std::mutex> lock(mutSession);
          session = s;
        }

        const auto started = std::chrono::steady_clock::now ();
        s->Run ();
        const auto uptime = std::chrono::steady_clock::now () - started;

        {
          std::lock_guard<std::mutex> lock(mutSession);
          session.reset ();
        }

        if (shouldStop)
          break;

        if (s->WasOpened () && uptime >= BACKOFF_RESET_AFTER)
          backoff = BACKOFF_INITIAL;

        LOG (WARNING)
            << "WebSocket to " << url << " is down, reconnecting in "
            << std::chrono::duration_cast<std::chrono::seconds> (backoff)
                   .count ()
            << "s (polling continues meanwhile)";
        InterruptibleSleep (
            std::chrono::duration_cast<std::chrono::milliseconds> (backoff));
        backoff = std::min<std::chrono::seconds> (2 * backoff, BACKOFF_MAX);
      }
  }

  void
  WatchdogLoop ()
  {
    while (!shouldStop)
      {
        InterruptibleSleep (std::chrono::duration_cast<
            std::chrono::milliseconds> (WATCHDOG_INTERVAL));
        if (shouldStop)
          break;

        const auto s = GetSession ();
        if (s == nullptr || !s->WasOpened ())
          continue;

        const auto staleMs = std::chrono::duration_cast<
            std::chrono::milliseconds> (STALE_TIMEOUT).count ();
        if (s->MessageAgeMs () > staleMs)
          {
            LOG (WARNING)
                << "WebSocket to " << url << " received no message for "
                << s->MessageAgeMs () / 1000
                << "s, assuming a dead connection and reconnecting";
            s->RequestStop ();
          }
      }
  }

public:

  explicit Connection (const std::string& u, Callbacks& c)
    : url(u), cb(c)
  {
    supervisor = std::make_unique<std::thread> (
        [this] () { SupervisorLoop (); });
    watchdog = std::make_unique<std::thread> (
        [this] () { WatchdogLoop (); });
  }

  ~Connection ()
  {
    shouldStop = true;
    cv.notify_all ();

    const auto s = GetSession ();
    if (s != nullptr)
      s->RequestStop ();

    watchdog->join ();
    supervisor->join ();
  }

  void
  EnablePending ()
  {
    pendingWanted = true;
    const auto s = GetSession ();
    if (s != nullptr)
      s->EnablePending ();
  }

};

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
