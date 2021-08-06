// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "controller.hpp"

#include "private/chainstate.hpp"
#include "private/sync.hpp"
#include "private/zmqpub.hpp"
#include "rpc-stubs/xayarpcserverstub.h"

#include <jsonrpccpp/common/errors.h>
#include <jsonrpccpp/common/exception.h>
#include <jsonrpccpp/server.h>
#include <jsonrpccpp/server/connectors/httpserver.h>

#include <glog/logging.h>

#include <experimental/filesystem>

#include <memory>
#include <sstream>

namespace xayax
{

namespace
{

namespace fs = std::experimental::filesystem;

/** Maximum number of attaches sent for a game_sendupdates.  */
constexpr unsigned MAX_BLOCK_ATTACHES = 1'024;

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

  /** HTTP connector for the RPC server.  */
  jsonrpc::HttpServer http;

  /** The RPC server run.  */
  std::unique_ptr<RpcServer> rpc;

  /* Callbacks from the base chain.  */
  void TipChanged () override;

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

public:

  explicit RpcServer (jsonrpc::AbstractServerConnector& conn, RunData& r)
    : XayaRpcServerStub(conn), run(r)
  {}

  Json::Value getzmqnotifications () override;
  void trackedgames (const std::string& cmd, const std::string& game) override;

  Json::Value getnetworkinfo () override;
  Json::Value getblockchaininfo () override;

  std::string getblockhash (int height) override;
  Json::Value getblockheader (const std::string& hash);

  Json::Value game_sendupdates (const std::string& from,
                                const std::string& gameId);

  Json::Value verifymessage (const std::string& addr, const std::string& msg,
                             const std::string& sgn) override;

  Json::Value getrawmempool () override;
  void stop () override;

};

Json::Value
Controller::RpcServer::getzmqnotifications ()
{
  std::lock_guard<std::mutex> lock(run.parent.mut);

  Json::Value res(Json::arrayValue);
  Json::Value cur(Json::objectValue);
  cur["type"] = "pubgameblocks";
  cur["address"] = run.parent.zmqAddr;
  res.append (cur);

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
    cachedVersion = run.parent.base.GetVersion ();
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
      cachedChain = run.parent.base.GetChain ();
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
  if (!run.chain.GetHashForHeight (height, hash))
    throw jsonrpc::JsonRpcException (-8, "block height out of range");

  return hash;
}

Json::Value
Controller::RpcServer::getblockheader (const std::string& hash)
{
  std::lock_guard<std::mutex> lock(run.mutChain);

  uint64_t height;
  if (!run.chain.GetHeightForHash (hash, height))
    throw jsonrpc::JsonRpcException (-5, "block not found");

  Json::Value res(Json::objectValue);
  res["hash"] = hash;
  res["height"] = static_cast<Json::Int64> (height);

  return res;
}

Json::Value
Controller::RpcServer::game_sendupdates (const std::string& from,
                                         const std::string& gameId)
{
  /* TODO: Actually use the gameId to filter notifications sent.  For now,
     we just trigger "default" ones.  This works as GSPs track their games
     anyway, but it may send too many notifications.  */

  std::ostringstream reqtoken;
  {
    std::lock_guard<std::mutex> lock(mut);
    ++requests;
    reqtoken << "request_" << requests;
  }

  std::vector<BlockData> detaches, attaches;
  run.PushZmqBlocks (from, {}, MAX_BLOCK_ATTACHES, reqtoken.str (),
                     detaches, attaches);

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
  /* FIXME: Implement based on BaseChain */
  return Json::Value ();
}

Json::Value
Controller::RpcServer::getrawmempool ()
{
  /* FIXME: Implement pending tracking */
  return Json::Value (Json::arrayValue);
}

void
Controller::RpcServer::stop ()
{
  run.parent.Stop ();
}

/* ************************************************************************** */

Controller::RunData::RunData (Controller& p, const std::string& dbFile)
  : parent(p), chain(dbFile),
    zmq(parent.zmqAddr),
    http(parent.rpcPort)
{
  CHECK (parent.run == nullptr);
  parent.run = this;

  sync = std::make_unique<Sync> (parent.base, chain, mutChain,
                                 parent.genesisHash, parent.genesisHeight);

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
Controller::RunData::TipChanged ()
{
  if (sync != nullptr)
    sync->NewBaseChainTip ();
}

void
Controller::RunData::TipUpdatedFrom (const std::string& oldTip,
                                     const std::vector<BlockData>& attaches)
{
  CHECK (!attaches.empty ());
  std::vector<BlockData> detach, queriedAttach;
  PushZmqBlocks (oldTip, attaches, 0, "", detach, queriedAttach);

  /* The pruning and sanityChecks flags in parent are never modified
     while the process is running, so it is fine to read them here without
     holding the parent mutex.  */

  std::lock_guard<std::mutex> lock(mutChain);

  if (parent.sanityChecks)
    chain.SanityCheck ();

  if (parent.pruning != -1)
    {
      CHECK_GE (parent.pruning, 0);
      const auto tipHeight = chain.GetTipHeight ();
      CHECK_GE (tipHeight, parent.genesisHeight);
      if (tipHeight > parent.pruning)
        chain.Prune (tipHeight - parent.pruning);
    }
}

void
Controller::RunData::PushZmqBlocks (const std::string& from,
                                    const std::vector<BlockData>& attaches,
                                    unsigned num,
                                    const std::string& reqtoken,
                                    std::vector<BlockData>& detach,
                                    std::vector<BlockData>& queriedAttach)
{
  std::lock_guard<std::mutex> lockChain(mutChain);

  detach.clear ();
  if (!from.empty () && !chain.GetForkBranch (from, detach))
    {
      /* This should not happen in practice.  For notifications triggered
         due to sync updates, the from block will always be one we have
         in the chain state; for notifications triggered by GSPs, it should
         also be a known block, unless the GSP was previously connected
         to a different instance.  */
      LOG (ERROR) << "Requested 'from' block " << from << " is known";
      return;
    }
  for (const auto& blk : detach)
    zmq.SendBlockDetach (blk, reqtoken);

  /* Find the height starting from which we need to send attach blocks from
     the main chain.  */
  uint64_t forkHeight;
  if (from.empty ())
    {
      /* This is the very first attach, starting from the genesis.  */
      CHECK (detach.empty ());
      forkHeight = parent.genesisHeight;
    }
  else if (detach.empty ())
    {
      /* The from block was already on the main chain, so we send from
         the block after it.  */
      CHECK (chain.GetHeightForHash (from, forkHeight));
      ++forkHeight;
    }
  else
    {
      /* We detached some blocks.  We start to send blocks from the main
         branch starting from the same height as the last detach.  */
      forkHeight = detach.back ().height;
    }

  /* If we have attach blocks already, look for the fork point in the
     list and send updates from there.  This is the case that applies
     during normal operation.  */
  if (!attaches.empty ())
    {
      /* A special case is if we just detached blocks.  In this case,
         attaches will have one element and that one will be the parent
         block of the detached ones; we are not actually attaching a block
         in this case.  */
      if (attaches.size () == 1 && !detach.empty ()
            && attaches.front ().hash == detach.back ().parent)
        return;

      bool foundForkPoint = false;
      for (const auto& blk : attaches)
        {
          if (blk.height == forkHeight)
            {
              foundForkPoint = true;
              if (from.empty ())
                CHECK_EQ (blk.hash, parent.genesisHash);
              else if (detach.empty ())
                CHECK_EQ (blk.parent, from);
              else
                CHECK_EQ (blk.parent, detach.back ().parent);
            }
          if (blk.height >= forkHeight)
            zmq.SendBlockAttach (blk, reqtoken);
        }
      CHECK (foundForkPoint);
      return;
    }

  /* Otherwise, this is the situation of an explicit RPC request for
     blocks.  We query the base chain for the attach blocks, up to
     the specified limit (or our chain tip).  */
  const auto tipHeight = chain.GetTipHeight ();
  CHECK_GE (tipHeight + 1, forkHeight);
  num = std::min<unsigned> (num, tipHeight + 1 - forkHeight);
  queriedAttach = parent.base.GetBlockRange (forkHeight, num);
  if (queriedAttach.empty ())
    return;

  /* It may happen that the chain returned does not actually match up with the
     detaches; in which case we will simply not send any attaches for now.
     The GSP's logic for recovering from missed ZMQ notifications takes care
     of that.  */
  bool mismatch;
  if (from.empty ())
    mismatch = (queriedAttach.front ().hash != parent.genesisHash);
  else if (detach.empty ())
    mismatch = (queriedAttach.front ().parent != from);
  else
    mismatch = (queriedAttach.front ().parent != detach.back ().parent);
  if (mismatch)
    {
      LOG (WARNING)
          << "Mismatch for detached and attached blocks, race condition?";
      queriedAttach.clear ();
      return;
    }

  /* We need to make sure that we are not sending any attaches
     for blocks that are not in our local chain state, so GSPs cannot get
     stuck on them in any case.  */
  uint64_t height;
  if (!chain.GetHeightForHash (queriedAttach.back ().hash, height))
    {
      LOG (WARNING)
          << "Attach blocks are not known to the local chain state yet";
      queriedAttach.clear ();
      return;
    }
  CHECK_EQ (height, queriedAttach.back ().height);

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
Controller::SetGenesis (const std::string& hash, const uint64_t height)
{
  std::lock_guard<std::mutex> lock(mut);
  CHECK (run == nullptr) << "Instance is already running";
  genesisHash = hash;
  genesisHeight = height;
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
Controller::EnableSanityChecks ()
{
  std::lock_guard<std::mutex> lock(mut);
  CHECK (run == nullptr) << "Instance is already running";
  sanityChecks = true;
  LOG (WARNING) << "Turning on sanity checks, this is slow";
}

void
Controller::EnablePruning (unsigned depth)
{
  std::lock_guard<std::mutex> lock(mut);
  CHECK (run == nullptr) << "Instance is already running";
  LOG (INFO) << "Turned on pruning with depth " << depth;
  pruning = depth;
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
