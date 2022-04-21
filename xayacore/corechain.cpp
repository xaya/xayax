// Copyright (C) 2021-2022 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "corechain.hpp"

#include "rpcutils.hpp"

#include "rpc-stubs/corerpcclient.h"

#include <eth-utils/hexutils.hpp>

#include <jsonrpccpp/client.h>
#include <jsonrpccpp/common/exception.h>
#include <zmq.hpp>

#include <glog/logging.h>

#include <atomic>
#include <algorithm>
#include <map>
#include <thread>

namespace xayax
{

/* ************************************************************************** */

namespace
{

/**
 * Converts a JSON transaction to a MoveData instance.  Returns false
 * if there is no actual move in the transaction.
 */
bool
GetMoveFromTx (const Json::Value& data, MoveData& mv)
{
  std::map<std::string, double> outAmounts;
  std::map<std::string, double> burns;
  Json::Value nameOp;
  for (const auto& out : data["vout"])
    {
      CHECK (out.isObject ());
      const auto& scriptPubKey = out["scriptPubKey"];
      CHECK (scriptPubKey.isObject ());

      if (scriptPubKey.isMember ("nameOp"))
        {
          CHECK (nameOp.isNull ()) << "Two name operations in tx";
          nameOp = scriptPubKey["nameOp"];
          continue;
        }

      if (scriptPubKey.isMember ("address"))
        {
          const auto& addrVal = scriptPubKey["address"];
          CHECK (addrVal.isString ());
          const std::string addr = addrVal.asString ();
          if (outAmounts.count (addr) == 0)
            outAmounts.emplace (addr, 0.0);
          outAmounts[addr] += out["value"].asDouble ();
        }

      if (scriptPubKey.isMember ("burn"))
        {
          const auto& burnVal = scriptPubKey["burn"];
          CHECK (burnVal.isString ());
          std::string burn;
          CHECK (ethutils::Unhexlify (burnVal.asString (), burn));
          if (burn.substr (0, 2) == "g/")
            {
              burn = burn.substr (2);
              if (burns.count (burn) == 0)
                burns.emplace (burn, 0.0);
              burns[burn] += out["value"].asDouble ();
            }
        }
    }

  if (nameOp.isNull ())
    return false;
  CHECK (nameOp.isObject ());

  CHECK_EQ (nameOp["name_encoding"].asString (), "utf8")
      << "Xaya Core's name_encoding should be UTF-8";
  CHECK_EQ (nameOp["value_encoding"].asString (), "utf8")
      << "Xaya Core's value_encoding should be UTF-8";

  /* This would happen if the name or value is invalid for the encoding.
     Xaya's consensus rules enforce that either of them are valid UTF-8,
     though, so this should never happen.  */
  CHECK (nameOp.isMember ("name") && nameOp.isMember ("value"))
      << "Name op does not contain name or value:\n"
      << nameOp;

  const std::string fullName = nameOp["name"].asString ();
  const size_t nsPos = fullName.find ('/');
  CHECK_NE (nsPos, std::string::npos)
      << "Name does not contain a namespace: " << fullName;

  mv.txid = data["txid"].asString ();
  mv.ns = fullName.substr (0, nsPos);
  mv.name = fullName.substr (nsPos + 1);
  mv.mv = nameOp["value"].asString ();

  mv.metadata = Json::Value (Json::objectValue);
  mv.metadata["btxid"] = data["btxid"].asString ();

  Json::Value outJson(Json::objectValue);
  for (const auto& entry : outAmounts)
    outJson[entry.first] = entry.second;
  mv.metadata["out"] = outJson;

  mv.burns.clear ();
  for (const auto& entry : burns)
    mv.burns.emplace (entry.first, Json::Value (entry.second));

  Json::Value inputs(Json::arrayValue);
  for (const auto& in : data["vin"])
    {
      Json::Value cur(Json::objectValue);
      cur["txid"] = in["txid"].asString ();
      cur["vout"] = in["vout"].asInt ();
      inputs.append (cur);
    }
  mv.metadata["inputs"] = inputs;

  return true;
}

/**
 * Converts the getblock JSON data to a BlockData instance.
 */
BlockData
ConstructBlockData (const Json::Value& data)
{
  CHECK (data.isObject ());

  BlockData res;

  res.hash = data["hash"].asString ();
  res.height = data["height"].asUInt64 ();
  if (data.isMember ("previousblockhash"))
    res.parent = data["previousblockhash"].asString ();
  res.rngseed = data["rngseed"].asString ();

  res.metadata = Json::Value (Json::objectValue);
  res.metadata["timestamp"] = data["time"].asInt64 ();
  res.metadata["mediantime"] = data["mediantime"].asInt64 ();

  for (const auto& tx : data["tx"])
    {
      MoveData mv;
      if (GetMoveFromTx (tx, mv))
        res.moves.push_back (std::move (mv));
    }

  return res;
}

using CoreRpc = RpcClient<CoreRpcClient, jsonrpc::JSONRPC_CLIENT_V1>;

/**
 * Queries for enabled ZMQ notifications on the Xaya Core node and
 * tries to find the address for a given type of notification.  If that type
 * is not enabled, returns the empty string instead.
 */
std::string
GetNotificationAddress (CoreRpc& rpc, const std::string& type)
{
  const auto notifications = rpc->getzmqnotifications ();
  CHECK (notifications.isArray ());

  for (const auto& n : notifications)
    {
      CHECK (n.isObject ());
      if (n["type"].asString () == type)
        return n["address"].asString ();
    }

  return "";
}

} // anonymous namespace

/* ************************************************************************** */

/**
 * ZMQ listener that can handle tip updates as well as pending transactions
 * from Xaya Core (pubhashblock and pubrawtx).
 */
class CoreChain::ZmqListener
{

private:

  /** Parent instance that the listener notifies about updates.  */
  CoreChain& parent;

  /**
   * Mutex used to synchronise the socket.  We need this when we add
   * a subscription topic to an already connected socket.
   */
  std::mutex mut;

  /** ZMQ socket for this listener.  */
  zmq::socket_t sock;

  /** Flag to indicate if the receiver thread should stop.  */
  std::atomic<bool> shouldStop;

  /** Background thread running the ZMQ receiver.  */
  std::unique_ptr<std::thread> receiver;

  /**
   * The RPC connection used from the rawtx handler.  This will only
   * be used from the receiver thread, so it is fine to have one instance
   * here that is shared between calls.
   */
  CoreRpc rpc;

  /**
   * Worker method that runs on the receiver thread.
   */
  void ReceiveLoop ();

  /**
   * Handles a tip-update notification payload.
   */
  void HandleHashBlock (const std::string& payload);

  /**
   * Handles a pending transaction payload.
   */
  void HandleRawTx (const std::string& payload);

public:

  /**
   * Constructs the listener, connecting it already to the given address
   * and starting the receiver thread.
   */
  explicit ZmqListener (CoreChain& p, zmq::context_t& ctx,
                        const std::string& addr)
    : parent(p), sock(ctx, ZMQ_SUB), shouldStop(false), rpc(parent.endpoint)
  {
    sock.connect (addr);
    receiver = std::make_unique<std::thread> ([this] ()
      {
        ReceiveLoop ();
      });
  }

  /**
   * Subscribes the socket to the given topic.
   */
  void
  Subscribe (const std::string& topic)
  {
    std::lock_guard<std::mutex> lock(mut);
    sock.set (zmq::sockopt::subscribe, topic);
  }

  /**
   * Destructs and stops the listener.
   */
  ~ZmqListener ()
  {
    CHECK (receiver != nullptr);
    shouldStop = true;
    receiver->join ();
    receiver.reset ();
    sock.close ();
  }

};

void
CoreChain::ZmqListener::ReceiveLoop ()
{
  while (!shouldStop)
    {
      std::unique_lock<std::mutex> lock(mut);

      zmq::message_t msg;
      if (!sock.recv (msg, zmq::recv_flags::dontwait))
        {
          /* No messages available.  Just sleep a bit and try again.  Make sure
             to release the lock so a subscription request from another thread
             can get through easily.  */
          lock.unlock ();
          std::this_thread::sleep_for (std::chrono::milliseconds (1));
          continue;
        }
      const std::string topic = msg.to_string ();

      /* Multipart messages are delivered atomically.  As long as
         there are more parts, they are guaranteed to be available
         now as well.  */
      CHECK (sock.get (zmq::sockopt::rcvmore));
      CHECK (sock.recv (msg, zmq::recv_flags::dontwait));
      const std::string payload = msg.to_string ();

      /* Ignore the sequence number.  */
      CHECK (sock.get (zmq::sockopt::rcvmore));
      CHECK (sock.recv (msg, zmq::recv_flags::dontwait));
      CHECK_EQ (msg.size (), 4);
      CHECK (!sock.get (zmq::sockopt::rcvmore));

      /* No need to keep the lock while processing.  */
      lock.unlock ();

      if (topic == "hashblock")
        HandleHashBlock (payload);
      else if (topic == "rawtx")
        HandleRawTx (payload);
      else
        {
          /* We should not have subscribed to any other topic (independent
             of what Xaya Core is configured to send).  */
          LOG (FATAL) << "Unexpected topic: " << topic;
        }
    }
}

void
CoreChain::ZmqListener::HandleHashBlock (const std::string& payload)
{
  parent.TipChanged (ethutils::Hexlify (payload));
}

void
CoreChain::ZmqListener::HandleRawTx (const std::string& payload)
{
  Json::Value tx;
  try
    {
      tx = rpc->decoderawtransaction (ethutils::Hexlify (payload));
    }
  catch (const jsonrpc::JsonRpcException& exc)
    {
      /* If the RPC is broken, just ignore the notification (which is
         fine to do anyway as pendings are just best-effort).  */
      LOG (WARNING) << "Xaya Core RPC error for pending move: " << exc.what ();
      return;
    }

  MoveData mv;
  if (GetMoveFromTx (tx, mv))
    parent.PendingMoves ({mv});
}

/* ************************************************************************** */

CoreChain::CoreChain (const std::string& ep)
  : endpoint(ep), zmqCtx(new zmq::context_t ())
{}

CoreChain::~CoreChain ()
{
  /* Explicitly destruct the ZMQ listeners / sockets before the context.
     The definition order in the class also ensures this, but let's be
     really explicit.  */
  listeners.clear ();
  zmqCtx.reset ();
}

CoreChain::ZmqListener&
CoreChain::GetListenerForAddress (const std::string& addr)
{
  const auto mit = listeners.find (addr);
  if (mit != listeners.end ())
    return *mit->second;

  LOG (INFO) << "Connecting ZMQ listener to Xaya Core at " << addr;
  auto inst = std::make_unique<ZmqListener> (*this, *zmqCtx, addr);
  auto& res = *inst;

  CHECK (listeners.emplace (addr, std::move (inst)).second);
  return res;
}

void
CoreChain::Start ()
{
  CoreRpc rpc(endpoint);

  /* We need at least Xaya Core 1.6, which supports burns in the
     scriptPubKey JSON and returns a single address (rather than addresses)
     for outputs that are solvable.  */
  const auto info = rpc->getnetworkinfo ();
  const uint64_t version = info["version"].asUInt64 ();
  CHECK_GE (version, 1'06'00'00)
      << "Xaya Core version is " << version << ", need at least 1.6.0";
  LOG (INFO) << "Connected to Xaya Core version " << version;

  const std::string addr = GetNotificationAddress (rpc, "pubhashblock");
  if (addr.empty ())
    LOG (WARNING)
        << "Xaya Core has no -zmqpubhashblock notifier,"
        << " relying on periodic polling only";
  else
    {
      LOG (INFO)
          << "Using -zmqpubhashblock notifier at " << addr
          << " for receiving tip updates from Xaya Core";
      GetListenerForAddress (addr).Subscribe ("hashblock");
    }
}

bool
CoreChain::EnablePending ()
{
  CoreRpc rpc(endpoint);

  const std::string addr = GetNotificationAddress (rpc, "pubrawtx");
  if (addr.empty ())
    {
      LOG (WARNING)
          << "Xaya Core has no -zmqpubrawtx notifier,"
          << " pending moves will not be detected";
      return false;
    }

  LOG (INFO)
      << "Using -zmqpubrawtx notifier at " << addr
      << " for pending moves from Xaya Core";
  GetListenerForAddress (addr).Subscribe ("rawtx");

  return true;
}

uint64_t
CoreChain::GetTipHeight ()
{
  CoreRpc rpc(endpoint);
  const auto blockchain = rpc->getblockchaininfo ();
  return blockchain["blocks"].asUInt64 ();
}

std::vector<BlockData>
CoreChain::GetBlockRange (const uint64_t start, const uint64_t count)
{
  if (count == 0)
    return {};

  CoreRpc rpc(endpoint);
  const uint64_t endHeight = start + count - 1;
  CHECK_GE (endHeight, start);

  /* We first determine the hash of the target block; which is either
     the best tip or a block at the target height.  Once this is done,
     we can go back from there and retrieve all block data leading up to
     it until the start height, in an atomic fashion.  */
  std::string endHash;
  while (true)
    {
      const auto blockchain = rpc->getblockchaininfo ();
      if (blockchain["blocks"].asUInt64 () < start)
        return {};
      if (blockchain["blocks"].asUInt64 () <= endHeight)
        {
          endHash = blockchain["bestblockhash"].asString ();
          break;
        }

      try
        {
          endHash = rpc->getblockhash (endHeight);
          break;
        }
      catch (const jsonrpc::JsonRpcException& exc)
        {
          if (exc.GetCode () != -8)
            throw;
          /* There is no block at this height.  This can happen due to
             a race condition when blocks were just detached.  Just try
             again in this case.  */
        }
    }

  std::vector<BlockData> res;
  do
    {
      const auto data = rpc->getblock (endHash, 2);
      auto cur = ConstructBlockData (data);

      CHECK_GE (cur.height, start);
      endHash = cur.parent;

      res.push_back (std::move (cur));
    }
  while (res.back ().height > start);

  std::reverse (res.begin (), res.end ());
  return res;
}

int64_t
CoreChain::GetMainchainHeight (const std::string& hash)
{
  CoreRpc rpc(endpoint);

  try
    {
      const auto data = rpc->getblockheader (hash);
      CHECK (data.isObject ());
      const auto conf = data["confirmations"];
      CHECK (conf.isInt64 ());
      if (conf.asInt64 () == -1)
        return -1;
      CHECK_GE (conf.asInt64 (), 0);
      return data["height"].asUInt64 ();
    }
  catch (const jsonrpc::JsonRpcException& exc)
    {
      LOG (WARNING) << "RPC error from getblockheader: " << exc.what ();
      return -1;
    }
}

std::vector<std::string>
CoreChain::GetMempool ()
{
  CoreRpc rpc(endpoint);

  const auto mempool = rpc->getrawmempool ();
  CHECK (mempool.isArray ());

  std::vector<std::string> res;
  for (const auto& txid : mempool)
    res.push_back (txid.asString ());

  return res;
}

bool
CoreChain::VerifyMessage (const std::string& msg, const std::string& signature,
                          std::string& addr)
{
  CoreRpc rpc(endpoint);

  Json::Value res;
  try
    {
      res = rpc->verifymessage ("", msg, signature);
    }
  catch (const jsonrpc::JsonRpcException& exc)
    {
      CHECK_EQ (exc.GetCode (), -3)
          << "Unexpected error from Xaya Core verifymessage: " << exc.what ();
      return false;
    }
  CHECK (res.isObject ());

  if (!res["valid"].asBool ())
    return false;

  addr = res["address"].asString ();
  return true;
}

std::string
CoreChain::GetChain ()
{
  CoreRpc rpc(endpoint);
  const auto info = rpc->getblockchaininfo ();
  return info["chain"].asString ();
}

uint64_t
CoreChain::GetVersion ()
{
  CoreRpc rpc(endpoint);
  const auto info = rpc->getnetworkinfo ();
  return info["version"].asUInt64 ();
}

/* ************************************************************************** */

} // namespace xayax
