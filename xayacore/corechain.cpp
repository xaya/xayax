// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "corechain.hpp"

#include "rpcutils.hpp"

#include "rpc-stubs/corerpcclient.h"

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
          CHECK (Unhexlify (burnVal.asString (), burn));
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

/**
 * Generic ZMQ listener class that runs a receive loop and reads
 * messages from Xaya Core.
 */
class GenericZmqListener
{

private:

  /** The topic string this is for.  */
  const std::string topic;

  /** ZMQ socket for this listener.  */
  zmq::socket_t sock;

  /** Flag to indicate if the receiver thread should stop.  */
  std::atomic<bool> shouldStop;

  /** Background thread running the ZMQ receiver.  */
  std::unique_ptr<std::thread> receiver;

  /**
   * Worker method that runs on the receiver thread.
   */
  void
  ReceiveLoop ()
  {
    while (!shouldStop)
      {
        zmq::message_t msg;
        if (!sock.recv (msg, zmq::recv_flags::dontwait))
          {
            /* No messages available.  Just sleep a bit and try again.  */
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
            continue;
          }

        CHECK_EQ (msg.to_string (), topic);

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

        HandleMessage (payload);
      }
  }

protected:

  /** Parent instance that subclasses notify about updates.  */
  CoreChain& parent;

  /**
   * Handles a particular message payload.
   */
  virtual void HandleMessage (const std::string& payload) = 0;

public:

  /**
   * Constructs the listener, connecting it already to the given address
   * and starting the receiver thread.
   */
  explicit GenericZmqListener (CoreChain& p, zmq::context_t& ctx,
                               const std::string& t, const std::string& addr)
    : topic(t), sock(ctx, ZMQ_SUB), shouldStop(false), parent(p)
  {
    sock.connect (addr);
    sock.set (zmq::sockopt::subscribe, topic);
    receiver = std::make_unique<std::thread> ([this] ()
      {
        ReceiveLoop ();
      });
  }

  /**
   * Destructs and stops the listener.
   */
  virtual ~GenericZmqListener ()
  {
    CHECK (receiver != nullptr);
    shouldStop = true;
    receiver->join ();
    receiver.reset ();
    sock.close ();
  }

};

} // anonymous namespace

/* ************************************************************************** */

/**
 * Simple ZMQ listener class for the Xaya Core -zmqpubhashblock endpoint,
 * which just notifies the BaseChain about a new tip to request every time
 * a notification is received.
 */
class CoreChain::ZmqBlockListener : public GenericZmqListener
{

protected:

  void
  HandleMessage (const std::string& payload) override
  {
    parent.TipChanged ();
  }

public:

  explicit ZmqBlockListener (CoreChain& p, zmq::context_t& ctx,
                             const std::string& addr)
    : GenericZmqListener(p, ctx, "hashblock", addr)
  {}

};

/**
 * Simple ZMQ listener class for the Xaya Core -zmqpubrawtx endpoint,
 * which notifies the BaseChain about new pending moves detected.
 */
class CoreChain::ZmqTxListener : public GenericZmqListener
{

private:

  /**
   * The RPC connection used from the message handler.  This will only
   * be used from the receiver thread, so it is fine to have one instance
   * here that is shared between calls.
   */
  CoreRpc rpc;

protected:

  void
  HandleMessage (const std::string& payload) override
  {
    const auto tx = rpc->decoderawtransaction (Hexlify (payload));
    MoveData mv;
    if (GetMoveFromTx (tx, mv))
      parent.PendingMoves ({mv});
  }

public:

  explicit ZmqTxListener (CoreChain& p, zmq::context_t& ctx,
                          const std::string& addr)
    : GenericZmqListener(p, ctx, "rawtx", addr),
      rpc(parent.endpoint)
  {}

};

/* ************************************************************************** */

CoreChain::CoreChain (const std::string& ep)
  : endpoint(ep), zmqCtx(new zmq::context_t ())
{}

CoreChain::~CoreChain () = default;

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
      blockListener
          = std::make_unique<ZmqBlockListener> (*this, *zmqCtx, addr);
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
          << "Xaya COre has no -zmqpubrawtx notifier,"
          << " pending moves will not be detected";
      return false;
    }

  LOG (INFO)
      << "Using -zmqpubrawtx notifier at " << addr
      << " for pending moves from Xaya Core";
  txListener = std::make_unique<ZmqTxListener> (*this, *zmqCtx, addr);

  return true;
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
