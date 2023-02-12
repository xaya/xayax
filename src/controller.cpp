// Copyright (C) 2021-2023 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "controller.hpp"

#include "private/chainstate.hpp"
#include "private/pending.hpp"
#include "private/sync.hpp"
#include "private/zmqpub.hpp"
#include "rpc-stubs/xayarpcserverstub.h"

#include <xayautil/base64.hpp>

#include <jsonrpccpp/common/errors.h>
#include <jsonrpccpp/common/exception.h>
#include <jsonrpccpp/server.h>
#include <jsonrpccpp/server/connectors/httpserver.h>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <experimental/filesystem>

#include <memory>
#include <sstream>

namespace xayax
{

DECLARE_int32 (xayax_block_range);

namespace
{

namespace fs = std::experimental::filesystem;

} // anonymous namespace

/* ************************************************************************** */

/**
 * Helper class with all the data that is used during a Run call of
 * our Controller.  This handles setup and cleanup with RAII.
 */
class Controller::RunData : private BaseChain::Callbacks,
                            private Sync::Callbacks
{

private:

  /** Associated Controller instance.  */
  Controller& parent;

  /** Mutex for the chainstate.  */
  std::mutex mutChain;

  Chainstate chain;
  std::unique_ptr<Sync> sync;
  ZmqPub zmq;
  PendingManager pendings;

  /** HTTP connector for the RPC server.  */
  jsonrpc::HttpServer http;

  /** The RPC server run.  */
  std::unique_ptr<RpcServer> rpc;

  /* Callbacks from the base chain.  */
  void TipChanged (const std::string& tip) override;
  void PendingMoves (const std::vector<MoveData>& moves) override;

  /* Callbacks from the sync worker.  */
  void TipUpdatedFrom (const std::string& oldTip,
                       const std::vector<BlockData>& attaches) override;

  /**
   * Sends ZMQ notifications for block detach and attach operations
   * starting from the given tip and onto the main chain.
   *
   * If attaches is filled in, those blocks must go back at least to the
   * fork point, and will be used to send the attach notifications.  Otherwise
   * the base chain is queried for up to num blocks starting from the fork
   * point and those are returned.
   *
   * The actual detach blocks will be returned in detaches.  If attaches
   * was not filled in and is instead retrieved, then those blocks will be
   * returned in queriedAttach.
   *
   * This method may throw in case of a base-chain error.
   */
  void PushZmqBlocks (const std::string& from,
                      const std::vector<BlockData>& attaches, unsigned num,
                      const std::string& reqtoken,
                      std::vector<BlockData>& detach,
                      std::vector<BlockData>& queriedAttach);

  friend class RpcServer;

public:

  explicit RunData (Controller& p, const std::string& dbFile);
  ~RunData ();

  /**
   * Disables the active sync task (used for testing).
   */
  void
  DisableSync ()
  {
    sync.reset ();
  }

};

/* ************************************************************************** */

/**
 * Local RPC server for the Xaya-Core-like RPC interface.
 */
class Controller::RpcServer : public XayaRpcServerStub
{

private:

  RunData& run;

  /** Lock for this instance (requests counter and cached version/chain).  */
  std::mutex mut;

  /** Counter used to generate request tokens.  */
  unsigned requests = 0;

  /** Cached chain string of the basechain.  */
  std::string cachedChain;
  /** Cached version of the basechain.  */
  int64_t cachedVersion = -1;

  /** The procedure for game_sendupdates with all three arguments set.  */
  const jsonrpc::Procedure procGameSendUpdates3;

  /**
   * Throws an internal JSON-RPC error to indicate that we had an issue
   * with the base chain given by the passed-in exception.
   */
  static void
  PropagateBaseChainError (const std::exception& exc)
  {
    LOG (WARNING) << "Base-chain error while handling RPC: " << exc.what ();
    std::ostringstream msg;
    msg << "Error with base chain: " << exc.what ();
    throw jsonrpc::JsonRpcException (jsonrpc::Errors::ERROR_RPC_INTERNAL_ERROR,
                                     msg.str ());
  }

public:

  explicit RpcServer (jsonrpc::AbstractServerConnector& conn, RunData& r);

  /**
   * libjson-rpc-cpp does not by itself support optional arguments, which
   * we need for game_sendupdates.  Thus we override the HandleMethodCall
   * method to manually handle calls to this method.
   */
  void HandleMethodCall (jsonrpc::Procedure& proc, const Json::Value& input,
                         Json::Value& output) override;

  Json::Value getzmqnotifications () override;
  void trackedgames (const std::string& cmd, const std::string& game) override;

  Json::Value getnetworkinfo () override;
  Json::Value getblockchaininfo () override;

  std::string getblockhash (int height) override;
  Json::Value getblockheader (const std::string& hash);

  Json::Value game_sendupdates () override;
  Json::Value game_sendupdates2 (const std::string& from,
                                 const std::string& gameId) override;
  Json::Value game_sendupdates3 (const std::string& from,
                                 const std::string& gameId,
                                 const std::string& to) override;

  Json::Value verifymessage (const std::string& addr, const std::string& msg,
                             const std::string& sgn) override;

  Json::Value getrawmempool () override;
  void stop () override;

};

Controller::RpcServer::RpcServer (jsonrpc::AbstractServerConnector& conn,
                                  RunData& r)
  : XayaRpcServerStub(conn), run(r),
    procGameSendUpdates3 ("game_sendupdates", jsonrpc::PARAMS_BY_NAME,
                          jsonrpc::JSON_OBJECT,
                          "fromblock", jsonrpc::JSON_STRING,
                          "gameid", jsonrpc::JSON_STRING,
                          "toblock", jsonrpc::JSON_STRING,
                          nullptr)
{}

void
Controller::RpcServer::HandleMethodCall (jsonrpc::Procedure& proc,
                                         const Json::Value& input,
                                         Json::Value& output)
{

  if (proc.GetProcedureName () == "game_sendupdates")
    {
      Json::Value fixedInput = input;
      if (fixedInput.isObject () && !fixedInput.isMember ("toblock"))
        fixedInput["toblock"] = "";

      if (!procGameSendUpdates3.ValdiateParameters (fixedInput))
        throw jsonrpc::JsonRpcException (
            jsonrpc::Errors::ERROR_RPC_INVALID_PARAMS,
            "invalid parameters for game_sendupdates");

      game_sendupdates3I (fixedInput, output);
      return;
    }

  XayaRpcServerStub::HandleMethodCall (proc, input, output);
}

Json::Value
Controller::RpcServer::getzmqnotifications ()
{
  std::lock_guard<std::mutex> lock(run.parent.mut);

  Json::Value res(Json::arrayValue);
  Json::Value cur(Json::objectValue);
  cur["type"] = "pubgameblocks";
  cur["address"] = run.parent.zmqAddr;
  res.append (cur);

  if (run.parent.pending)
    {
      cur["type"] = "pubgamepending";
      res.append (cur);
    }

  return res;
}

void
Controller::RpcServer::trackedgames (const std::string& cmd,
                                     const std::string& game)
{
  if (cmd == "add")
    run.zmq.TrackGame (game);
  else if (cmd == "remove")
    run.zmq.UntrackGame (game);
}

Json::Value
Controller::RpcServer::getnetworkinfo ()
{
  Json::Value res(Json::objectValue);

  std::lock_guard<std::mutex> lock(mut);
  if (cachedVersion == -1)
    try
      {
        cachedVersion = run.parent.base.GetVersion ();
      }
    catch (const std::exception& exc)
      {
        PropagateBaseChainError (exc);
      }
  CHECK_GE (cachedVersion, -1);
  res["version"] = static_cast<Json::Int64> (cachedVersion);

  return res;
}

Json::Value
Controller::RpcServer::getblockchaininfo ()
{
  Json::Value res(Json::objectValue);

  {
    std::lock_guard<std::mutex> lock(mut);
    if (cachedChain.empty ())
      try
        {
          cachedChain = run.parent.base.GetChain ();
        }
      catch (const std::exception& exc)
        {
          PropagateBaseChainError (exc);
        }
    CHECK (!cachedChain.empty ());
    res["chain"] = cachedChain;
  }

  std::lock_guard<std::mutex> lockChain(run.mutChain);
  const auto tipHeight = run.chain.GetTipHeight ();
  if (tipHeight == -1)
    {
      res["blocks"] = -1;
      res["bestblockhash"] = "";
    }
  else
    {
      CHECK_GE (tipHeight, 0);
      res["blocks"] = static_cast<Json::Int64> (tipHeight);

      std::string tipHash;
      CHECK (run.chain.GetHashForHeight (tipHeight, tipHash));
      res["bestblockhash"] = tipHash;
    }

  return res;
}

std::string
Controller::RpcServer::getblockhash (const int height)
{
  std::lock_guard<std::mutex> lock(run.mutChain);

  std::string hash;
  if (run.chain.GetHashForHeight (height, hash))
    return hash;

  /* This might be a pruned block.  In this case, we query the main chain
     for it.  */

  if (height >= run.chain.GetLowestUnprunedHeight ())
    throw jsonrpc::JsonRpcException (-8, "block height out of range");

  std::vector<BlockData> blocks;
  try
    {
      blocks = run.parent.base.GetBlockRange (height, 1);
    }
  catch (const std::exception& exc)
    {
      PropagateBaseChainError (exc);
    }

  if (blocks.empty ())
    throw jsonrpc::JsonRpcException (-8, "block height out of range");

  CHECK_EQ (blocks.size (), 1);
  return blocks[0].hash;
}

Json::Value
Controller::RpcServer::getblockheader (const std::string& hash)
{
  std::lock_guard<std::mutex> lock(run.mutChain);

  Json::Value res(Json::objectValue);
  res["hash"] = hash;

  uint64_t height;
  if (run.chain.GetHeightForHash (hash, height))
    {
      res["height"] = static_cast<Json::Int64> (height);
      return res;
    }

  /* Check the base chain to see if this might be a pruned block.  */
  try
    {
      const int64_t baseHeight = run.parent.base.GetMainchainHeight (hash);
      if (baseHeight != -1)
        {
          CHECK_GE (baseHeight, 0);
          res["height"] = static_cast<Json::Int64> (baseHeight);
          return res;
        }
    }
  catch (const std::exception& exc)
    {
      PropagateBaseChainError (exc);
    }

  throw jsonrpc::JsonRpcException (-5, "block not found");
}

Json::Value
Controller::RpcServer::game_sendupdates ()
{
  LOG (FATAL) << "game_sendupdates call should have been intercepted";
}

Json::Value
Controller::RpcServer::game_sendupdates2 (const std::string& from,
                                          const std::string& gameId)
{
  return game_sendupdates3 (from, gameId, "");
}

Json::Value
Controller::RpcServer::game_sendupdates3 (const std::string& from,
                                          const std::string& gameId,
                                          const std::string& to)
{
  /* TODO: Actually use the gameId to filter notifications sent.  For now,
     we just trigger "default" ones.  This works as GSPs track their games
     anyway, but it may send too many notifications.  */

  CHECK_EQ (to, "");

  std::ostringstream reqtoken;
  {
    std::lock_guard<std::mutex> lock(mut);
    ++requests;
    reqtoken << "request_" << requests;
  }

  std::vector<BlockData> detaches, attaches;
  try
    {
      std::lock_guard<std::mutex> lock(run.mutChain);
      run.PushZmqBlocks (from, {}, FLAGS_xayax_block_range, reqtoken.str (),
                         detaches, attaches);
    }
  catch (const std::exception& exc)
    {
      PropagateBaseChainError (exc);
    }

  std::string toBlock;
  if (!attaches.empty ())
    toBlock = attaches.back ().hash;
  else if (!detaches.empty ())
    toBlock = detaches.back ().parent;
  else
    toBlock = from;

  Json::Value steps(Json::objectValue);
  steps["detach"] = static_cast<Json::Int64> (detaches.size ());
  steps["attach"] = static_cast<Json::Int64> (attaches.size ());

  Json::Value res(Json::objectValue);
  res["reqtoken"] = reqtoken.str ();
  res["toblock"] = toBlock;
  res["steps"] = steps;

  return res;
}

Json::Value
Controller::RpcServer::verifymessage (const std::string& addr,
                                      const std::string& msg,
                                      const std::string& sgn)
{
  /* The RPC argument for the signature is always base64 encoded (as with
     Xaya Core).  The base chains expect "raw byte" signatures.  */
  std::string rawSgn;
  if (!xaya::DecodeBase64 (sgn, rawSgn))
    {
      LOG (WARNING) << "Signature is not base64: " << sgn;
      throw jsonrpc::JsonRpcException (
          jsonrpc::Errors::ERROR_RPC_INVALID_PARAMS,
          "signature is not base64-encoded");
    }

  /* If addr is passed as "", then this RPC is supposed to do recovery
     and return the signer address.  Otherwise, it should just return true
     or false depending on validity for the given address.  This is what the
     RPC does in Xaya Core.  */
  const bool addrRecovery = addr.empty ();

  std::string signerAddr;
  bool ok;
  try
    {
      ok = run.parent.base.VerifyMessage (msg, rawSgn, signerAddr);
    }
  catch (const std::exception& exc)
    {
      PropagateBaseChainError (exc);
    }

  if (!ok)
    {
      if (!addrRecovery)
        return false;

      Json::Value res(Json::objectValue);
      res["valid"] = false;
      return res;
    }

  if (!addrRecovery)
    return signerAddr == addr;

  Json::Value res(Json::objectValue);
  res["valid"] = true;
  res["address"] = signerAddr;
  return res;
}

Json::Value
Controller::RpcServer::getrawmempool ()
{
  std::vector<std::string> mempool;
  try
    {
      mempool = run.parent.base.GetMempool ();
    }
  catch (const std::exception& exc)
    {
      PropagateBaseChainError (exc);
    }

  Json::Value res(Json::arrayValue);
  for (const auto& txid : mempool)
    res.append (txid);

  return res;
}

void
Controller::RpcServer::stop ()
{
  run.parent.Stop ();
}

/* ************************************************************************** */

Controller::RunData::RunData (Controller& p, const std::string& dbFile)
  : parent(p), chain(dbFile),
    zmq(parent.zmqAddr), pendings(zmq),
    http(parent.rpcPort)
{
  CHECK (parent.run == nullptr);
  parent.run = this;

  sync = std::make_unique<Sync> (parent.base, chain, mutChain,
                                 parent.maxReorgDepth);

  for (const auto& g : parent.trackedGames)
    zmq.TrackGame (g);

  if (parent.rpcListenLocally)
    http.BindLocalhost ();

  rpc = std::make_unique<RpcServer> (http, *this);
  rpc->StartListening ();

  parent.ServersStarted ();

  parent.base.SetCallbacks (this);
  sync->SetCallbacks (this);
  sync->Start ();
}

Controller::RunData::~RunData ()
{
  CHECK (parent.run == this);
  parent.run = nullptr;

  rpc->StopListening ();

  parent.base.SetCallbacks (nullptr);
  if (sync != nullptr)
    sync->SetCallbacks (nullptr);
}

void
Controller::RunData::TipChanged (const std::string& tip)
{
  pendings.TipChanged (tip);
  if (sync != nullptr)
    sync->NewBaseChainTip ();
}

void
Controller::RunData::PendingMoves (const std::vector<MoveData>& moves)
{
  pendings.PendingMoves (moves);
}

void
Controller::RunData::TipUpdatedFrom (const std::string& oldTip,
                                     const std::vector<BlockData>& attaches)
{
  CHECK (!attaches.empty ());
  std::vector<BlockData> detach, queriedAttach;
  try
    {
      PushZmqBlocks (oldTip, attaches, 0, "", detach, queriedAttach);
    }
  catch (const std::exception& exc)
    {
      LOG (WARNING) << "Error while pushing ZMQ block updates: " << exc.what ();
      /* Just ignore pushing the blocks for now.  GSPs are able to recover
         from missing ZMQ notifications anyway.  */
    }

  /* Potentially push queued pending moves after the block attach/detach
     notifications have been sent to GSPs.  */
  pendings.ChainstateTipChanged (attaches.back ().hash);

  /* The pruning and sanityChecks flags in parent are never modified
     while the process is running, so it is fine to read them here without
     holding the parent mutex.  */

  if (parent.sanityChecks)
    chain.SanityCheck ();

  CHECK_GE (parent.maxReorgDepth, 0);
  const auto tipHeight = chain.GetTipHeight ();
  if (tipHeight > parent.maxReorgDepth + 1)
    chain.Prune (tipHeight - parent.maxReorgDepth - 1);
}

void
Controller::RunData::PushZmqBlocks (const std::string& from,
                                    const std::vector<BlockData>& attaches,
                                    unsigned num,
                                    const std::string& reqtoken,
                                    std::vector<BlockData>& detach,
                                    std::vector<BlockData>& queriedAttach)
{
  /* If this is a sequence of the very first blocks / blocks re-imported
     not matching up to the current chain, just push the attach blocks.  */
  if (from.empty ())
    {
      LOG_IF (WARNING, attaches.empty ())
          << "Requested ZMQ blocks without explicit from and no attaches";
      for (const auto& blk : attaches)
        zmq.SendBlockAttach (blk, reqtoken);
      return;
    }

  detach.clear ();
  int64_t mainchainHeight = -1;
  if (!chain.GetForkBranch (from, detach))
    {
      /* The block is not known, which most likely means that it is
         an old main chain block that was pruned.  */
      mainchainHeight = parent.base.GetMainchainHeight (from);
      if (mainchainHeight == -1)
        {
          /* Usually, the 'from' block is one that was previously a best tip
             (and thus either the local chainstate is syncing from it due to
             a tip update, or a GSP requests blocks from what it previously
             got as best tip from Xaya X).  Thus the current situation should
             only happen due to a reorg beyond the pruning depth.  */
          LOG (ERROR)
              << "Requested 'from' block " << from
              << " is unknown and also not on the main chain";
          return;
        }
    }
  for (const auto& blk : detach)
    zmq.SendBlockDetach (blk, reqtoken);

  /* Find the height starting from which we need to send attach blocks from
     the main chain.  forkPoint will be the main-chain block to which we
     detach or the from block if we start on the main-chain.  */
  uint64_t forkHeight;
  std::string forkPoint;
  if (mainchainHeight != -1)
    {
      /* The fork is going back to a pruned block on main chain.  */
      forkHeight = mainchainHeight;
      forkPoint = from;
    }
  else if (detach.empty ())
    {
      /* The from block was already on the main chain, so we send from
         the block after it.  */
      CHECK (chain.GetHeightForHash (from, forkHeight));
      forkPoint = from;
    }
  else
    {
      /* We detached some blocks.  We start to send blocks from the main
         branch starting from the same height as the last detach.  */
      forkHeight = detach.back ().height - 1;
      forkPoint = detach.back ().parent;
    }

  /* If we have attach blocks already, look for the fork point in the
     list and send updates from there.  This is the case that applies
     during normal operation.  */
  if (!attaches.empty ())
    {
      /* A special case is if we just detached blocks.  In this case,
         the last attached block will be the new tip, and the parent
         of the last detached one.  */
      if (!detach.empty () && attaches.back ().hash == detach.back ().parent)
        return;

      bool foundForkPoint = false;
      for (const auto& blk : attaches)
        {
          if (blk.height == forkHeight + 1)
            {
              foundForkPoint = true;
              CHECK_EQ (blk.parent, forkPoint);
            }
          if (blk.height > forkHeight)
            zmq.SendBlockAttach (blk, reqtoken);
        }
      CHECK (foundForkPoint);
      return;
    }

  /* Otherwise, this is the situation of an explicit RPC request for
     blocks.  We query the base chain for the attach blocks, up to
     the specified limit (or our chain tip).  */
  const auto tipHeight = chain.GetTipHeight ();
  CHECK_GE (tipHeight, forkHeight);
  num = std::min<unsigned> (num, tipHeight - forkHeight);
  queriedAttach = parent.base.GetBlockRange (forkHeight + 1, num);
  if (queriedAttach.empty ())
    return;

  /* It may happen that the chain returned does not actually match up with the
     detaches; in which case we will simply not send any attaches for now.
     The GSP's logic for recovering from missed ZMQ notifications takes care
     of that.  */
  const bool mismatch = (queriedAttach.front ().parent != forkPoint);
  if (mismatch)
    {
      LOG (WARNING)
          << "Mismatch for detached and attached blocks, race condition?";
      queriedAttach.clear ();
      return;
    }

  /* We need to make sure that we are not sending any attaches
     for blocks that are not in our local chain state, so GSPs cannot get
     stuck on them in any case.  If they are before our pruning depth,
     then it should be fine.  */
  const int64_t pruningDepth = chain.GetLowestUnprunedHeight ();
  CHECK_GE (pruningDepth, 0);
  if (queriedAttach.back ().height >= static_cast<uint64_t> (pruningDepth))
    {
      uint64_t height;
      if (!chain.GetHeightForHash (queriedAttach.back ().hash, height))
        {
          LOG (WARNING)
              << "Attach blocks are not known to the local chain state yet";
          queriedAttach.clear ();
          return;
        }
      CHECK_EQ (height, queriedAttach.back ().height);
    }

  for (const auto& blk : queriedAttach)
    zmq.SendBlockAttach (blk, reqtoken);
}

/* ************************************************************************** */

Controller::Controller (BaseChain& bc, const std::string& dir)
  : base(bc), dataDir(dir)
{}

Controller::~Controller ()
{
  std::lock_guard<std::mutex> lock(mut);
  CHECK (run == nullptr) << "Instance is still running";
}

void
Controller::SetZmqEndpoint (const std::string& addr)
{
  std::lock_guard<std::mutex> lock(mut);
  CHECK (run == nullptr) << "Instance is already running";
  zmqAddr = addr;
}

void
Controller::SetRpcBinding (const int p, const bool local)
{
  std::lock_guard<std::mutex> lock(mut);
  CHECK (run == nullptr) << "Instance is already running";
  rpcPort = p;
  rpcListenLocally = local;
}

void
Controller::EnablePending ()
{
  std::lock_guard<std::mutex> lock(mut);
  CHECK (run == nullptr) << "Instance is already running";

  if (pending)
    return;

  if (base.EnablePending ())
    {
      pending = true;
      LOG (INFO) << "Tracking pending moves";
    }
  else
    LOG (WARNING) << "BaseChain does not support pending moves";
}

void
Controller::EnableSanityChecks ()
{
  std::lock_guard<std::mutex> lock(mut);
  CHECK (run == nullptr) << "Instance is already running";
  sanityChecks = true;
  LOG (WARNING) << "Turning on sanity checks, this is slow";
}

void
Controller::SetMaxReorgDepth (unsigned depth)
{
  std::lock_guard<std::mutex> lock(mut);
  CHECK (run == nullptr) << "Instance is already running";
  LOG (INFO) << "Setting maximum reorg depth to " << depth;
  maxReorgDepth = depth;
}

void
Controller::TrackGame (const std::string& gameId)
{
  std::lock_guard<std::mutex> lock(mut);
  CHECK (run == nullptr) << "Instance is already running";
  trackedGames.insert (gameId);
}

void
Controller::DisableSyncForTesting ()
{
  std::lock_guard<std::mutex> lock(mut);
  CHECK (run != nullptr) << "Instance is not running";
  run->DisableSync ();
}

void
Controller::Stop ()
{
  std::lock_guard<std::mutex> lock(mut);
  shouldStop = true;
  cv.notify_all ();
}

void
Controller::Run ()
{
  std::unique_lock<std::mutex> lock(mut);

  CHECK (run == nullptr) << "Instance is already running";
  CHECK_GE (maxReorgDepth, 0) << "No maximum reorg depth has been configured";
  CHECK (!zmqAddr.empty ()) << "No ZMQ address has been configured";
  CHECK_GT (rpcPort, -1) << "No RPC port has been configured";

  const fs::path pathDataDir = fs::path (dataDir) / base.GetChain ();
  if (fs::is_directory (pathDataDir))
    LOG (INFO) << "Using existing data directory: " << pathDataDir;
  else
    {
      LOG (INFO) << "Creating data directory: " << pathDataDir;
      CHECK (fs::create_directories (pathDataDir));
    }
  const fs::path pathDb = pathDataDir / "chainstate.sqlite";

  RunData run(*this, pathDb.string ());

  shouldStop = false;
  while (!shouldStop)
    cv.wait (lock);

  /* Wait a tiny bit of extra time before shutting down.  This gives
     e.g. the RPC server time to finish the stop() call that was made
     without erroring on a closed connection.  */
  std::this_thread::sleep_for (std::chrono::milliseconds (10));
}

/* ************************************************************************** */

} // namespace xayax
