// Copyright (C) 2021-2024 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "controller.hpp"

#include "rpc-stubs/xayarpcclient.h"
#include "testutils.hpp"

#include <jsonrpccpp/client.h>
#include <jsonrpccpp/client/connectors/httpclient.h>
#include <jsonrpccpp/common/exception.h>

#include <glog/logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <experimental/filesystem>

#include <sstream>

namespace xayax
{

DECLARE_int32 (xayax_block_range);

namespace
{

namespace fs = std::experimental::filesystem;

using testing::ElementsAre;

/**
 * Address for the ZMQ socket in tests.  While we could use some non-TCP
 * method here for testing, using TCP is closer to what will be used in
 * production (and doesn't really hurt us much).
 */
constexpr const char* ZMQ_ADDR = "tcp://127.0.0.1:49837";

/** Port for the local test RPC server.  */
constexpr int RPC_PORT = 49'838;

/** Our test game ID.  */
const std::string GAME_ID = "game";

/**
 * Constructs the RPC endpoint as string.
 */
std::string
GetRpcEndpoint ()
{
  std::ostringstream out;
  out << "http://localhost:" << RPC_PORT;
  return out.str ();
}

/* ************************************************************************** */

} // anonymous namespace

/**
 * Test fixture for the controller.  We use a temporary (but on-disk)
 * file for the database, so that we can test permanence of data if the
 * controller is stopped and restarted.
 */
class ControllerTests : public testing::Test
{

private:

  class TestController;

  /** Path of the temporary data directory.  */
  fs::path dataDir;

  /**
   * Our controller instance.  We use a unique_ptr so that it can be
   * stopped and recreated to test data permanence, catching up and
   * things like that.
   */
  std::unique_ptr<TestController> controller;

  /**
   * If we have a controller active, this is the thread that blocks on its
   * Run method.
   */
  std::unique_ptr<std::thread> runner;

protected:

  TestBaseChain base;

  /** The genesis block we use in the base chain (and controller).  */
  const BlockData genesis;

  ControllerTests ()
    : genesis(base.NewGenesis (0))
  {
    dataDir = std::tmpnam (nullptr);
    LOG (INFO) << "Using temporary data directory: " << dataDir;

    base.SetGenesis (genesis);
    base.Start ();

    Restart ();
  }

  ~ControllerTests ()
  {
    StopController ();
    fs::remove_all (dataDir);
  }

  /**
   * Disables syncing in the controller.
   */
  void DisableSync ();

  /**
   * Stops the controller instance we currently have and destructs it.
   */
  void StopController ();

  /**
   * Recreates the Controller instance, including starting it.  If there is
   * already an instance, it will be stopped and destructed first.
   */
  void Restart (unsigned maxReorgDepth = 1'000'000, bool pending = false);

  /**
   * Expects ZMQ messages (first detaches and then attaches) to be received
   * for the given blocks.  Only the block hashes are verified.
   */
  void ExpectZmq (const std::vector<BlockData>& detach,
                  const std::vector<BlockData>& attach,
                  const std::string& reqtoken = "");

  /**
   * Waits for the ZMQ attach message of the given block, ignoring all
   * before that.
   */
  void WaitForZmqTip (const BlockData& tip);

  /**
   * Awaits n pending ZMQ messages and returns them.
   */
  std::vector<Json::Value> AwaitPending (size_t num);

  /**
   * Builds up a move-data instance from the given data.  The actual
   * move data is built with our GAME_ID and the given JSON value.
   */
  static MoveData Move (const std::string& ns, const std::string& name,
                        const std::string& txid, const Json::Value& mv);

};

/**
 * Test controller that has its own ZMQ subscriber integrated, and connects
 * it once the publisher has been set up.
 */
class ControllerTests::TestController : public Controller
{

private:

  std::mutex mut;
  std::condition_variable cv;

protected:

  void
  ServersStarted () override
  {
    std::lock_guard<std::mutex> lock(mut);
    sub = std::make_unique<TestZmqSubscriber> (ZMQ_ADDR);
    SleepSome ();
    cv.notify_all ();
  }

public:

  std::unique_ptr<TestZmqSubscriber> sub;

  TestController (ControllerTests& tc)
    : Controller(tc.base, tc.dataDir.string ())
  {
    SetZmqEndpoint (ZMQ_ADDR);
    SetRpcBinding (RPC_PORT, true);
    EnableSanityChecks ();
    TrackGame (GAME_ID);
  }

  ~TestController ()
  {
    /* Sleep some time before destructing the ZMQ subscriber to make
       sure it would receive any unexpected extra messages.  */
    SleepSome ();
  }

  /**
   * Waits until ServersStarted has been called.
   */
  void
  WaitUntilStartup ()
  {
    std::unique_lock<std::mutex> lock(mut);
    while (sub == nullptr)
      cv.wait (lock);
  }

};

void
ControllerTests::DisableSync ()
{
  controller->DisableSyncForTesting ();
}

void
ControllerTests::Restart (const unsigned maxReorgDepth, const bool pending)
{
  StopController ();

  controller = std::make_unique<TestController> (*this);
  controller->SetMaxReorgDepth (maxReorgDepth);
  if (pending)
    controller->EnablePending ();

  runner = std::make_unique<std::thread> ([this] ()
    {
      controller->Run ();
    });

  controller->WaitUntilStartup ();
}

void
ControllerTests::StopController ()
{
  if (controller == nullptr)
    return;

  controller->Stop ();
  runner->join ();
  runner.reset ();
  controller.reset ();
}

void
ControllerTests::ExpectZmq (const std::vector<BlockData>& detach,
                            const std::vector<BlockData>& attach,
                            const std::string& reqtoken)
{
  auto actual
      = controller->sub->AwaitMessages ("game-block-detach json " + GAME_ID,
                                        detach.size ());
  CHECK_EQ (actual.size (), detach.size ());
  for (unsigned i = 0; i < actual.size (); ++i)
    {
      ASSERT_EQ (actual[i]["block"]["hash"], detach[i].hash);
      ASSERT_EQ (actual[i]["reqtoken"].asString (), reqtoken);
    }

  actual
      = controller->sub->AwaitMessages ("game-block-attach json " + GAME_ID,
                                        attach.size ());
  CHECK_EQ (actual.size (), attach.size ());
  for (unsigned i = 0; i < actual.size (); ++i)
    {
      ASSERT_EQ (actual[i]["block"]["hash"], attach[i].hash);
      ASSERT_EQ (actual[i]["reqtoken"].asString (), reqtoken);
    }
}

void
ControllerTests::WaitForZmqTip (const BlockData& tip)
{
  while (true)
    {
      const auto attaches
          = controller->sub->AwaitMessages ("game-block-attach json " + GAME_ID,
                                            1);
      CHECK_EQ (attaches.size (), 1);
      const auto& msg = attaches.front ();
      ASSERT_EQ (msg["reqtoken"].asString (), "");
      if (msg["block"]["hash"].asString () == tip.hash)
        {
          controller->sub->ForgetAll ();
          return;
        }
    }
}

std::vector<Json::Value>
ControllerTests::AwaitPending (const size_t num)
{
  const std::string topic = "game-pending-move json " + GAME_ID;
  return controller->sub->AwaitMessages (topic, num);
}

MoveData
ControllerTests::Move (const std::string& ns, const std::string& name,
                       const std::string& txid, const Json::Value& mv)
{
  MoveData res;
  res.ns = ns;
  res.name = name;
  res.txid = txid;

  Json::Value fullMove = ParseJson (R"({"g":{}})");
  fullMove["g"][GAME_ID] = mv;

  std::ostringstream out;
  out << fullMove;
  res.mv = out.str ();

  return res;
}

namespace
{

/* ************************************************************************** */

TEST_F (ControllerTests, BasicSyncing)
{
  const auto a = base.SetTip (base.NewBlock ());
  const auto b = base.SetTip (base.NewBlock ());
  const auto c = base.SetTip (base.NewBlock ());
  ExpectZmq ({}, {genesis, a, b, c});
}

TEST_F (ControllerTests, Reorg)
{
  const auto a = base.SetTip (base.NewBlock ());
  const auto b = base.SetTip (base.NewBlock ());
  ExpectZmq ({}, {genesis, a, b});
  const auto branch1 = base.AttachBranch (a.hash, 2);
  ExpectZmq ({b}, {branch1[0], branch1[1]});
  const auto branch2 = base.AttachBranch (b.hash, 2);
  ExpectZmq ({branch1[1], branch1[0]}, {b, branch2[0], branch2[1]});
}

TEST_F (ControllerTests, Restart)
{
  const auto a = base.SetTip (base.NewBlock ());
  ExpectZmq ({}, {genesis, a});
  StopController ();
  const auto b = base.SetTip (base.NewBlock (genesis.hash));
  const auto c = base.SetTip (base.NewBlock ());
  Restart ();
  ExpectZmq ({a}, {b, c});
}

TEST_F (ControllerTests, ReorgBeyondPruningDepth)
{
  const auto a = base.SetTip (base.NewBlock ());
  const auto b = base.SetTip (base.NewBlock ());
  const auto branch1 = base.AttachBranch (b.hash, 2);
  ExpectZmq ({}, {genesis, a, b, branch1[0], branch1[1]});
  Restart (2);
  const auto branch2 = base.AttachBranch (b.hash, 2);
  ExpectZmq ({branch1[1], branch1[0]}, {branch2[0], branch2[1]});
  StopController ();

  testing::FLAGS_gtest_death_test_style = "threadsafe";
  EXPECT_DEATH (
    {
      Restart ();
      const auto c = base.SetTip (base.NewBlock (a.hash));
      ExpectZmq ({branch2[1], branch2[0], b}, {c});
    }, "Reorg beyond pruning depth");
}

TEST_F (ControllerTests, ZeroReorgDepth)
{
  ExpectZmq ({}, {genesis});
  Restart (0);
  const auto a = base.SetTip (base.NewBlock ());
  const auto b = base.SetTip (base.NewBlock ());
  ExpectZmq ({}, {a, b});
}

TEST_F (ControllerTests, PerChainDataDir)
{
  ExpectZmq ({}, {genesis});
  StopController ();

  base.SetChain ("foo");
  const auto a = base.SetTip (base.NewBlock ());
  Restart ();
  ExpectZmq ({}, {genesis, a});
  StopController ();

  base.SetChain ("bar");
  const auto b = base.SetTip (base.NewBlock (genesis.hash));
  Restart ();
  ExpectZmq ({}, {genesis, b});
  StopController ();

  base.SetChain ("foo");
  Restart ();
  ExpectZmq ({a}, {b});
}

/* ************************************************************************** */

class ControllerRpcTests : public ControllerTests
{

private:

  /** HTTP client connector for the RPC server.  */
  jsonrpc::HttpClient httpClient;

protected:

  /** RPC client connected to the test controller's server.  */
  XayaRpcClient rpc;

  ControllerRpcTests ()
    : httpClient(GetRpcEndpoint ()), rpc(httpClient)
  {
    WaitForZmqTip (genesis);
  }

};

TEST_F (ControllerRpcTests, GetZmqNotifications)
{
  auto expected = ParseJson (R"([
    {
      "type": "pubgameblocks"
    },
    {
      "type": "pubgamepending"
    }
  ])");
  for (auto& e : expected)
    e["address"] = ZMQ_ADDR;

  Restart (1'000'000, true);
  EXPECT_EQ (rpc.getzmqnotifications (), expected);

  expected.resize (1);
  Restart (1'000'000, false);
  EXPECT_EQ (rpc.getzmqnotifications (), expected);
}

TEST_F (ControllerRpcTests, TrackedGames)
{
  rpc.trackedgames ("remove", GAME_ID);
  const auto a = base.SetTip (base.NewBlock ());
  /* Wait for the notification to be triggered (but it won't send anything
     as we are not tracking any game).  */
  SleepSome ();

  rpc.trackedgames ("add", GAME_ID);
  const auto b = base.SetTip (base.NewBlock ());
  ExpectZmq ({}, {b});
}

TEST_F (ControllerRpcTests, GetNetworkInfo)
{
  /* The first call to getnetworkinfo will cache the version.  */
  base.SetVersion (42);
  rpc.getnetworkinfo ();
  base.SetVersion (100);

  EXPECT_EQ (rpc.getnetworkinfo (), ParseJson (R"({
    "version": 42
  })"));
}

TEST_F (ControllerRpcTests, GetBlockchainInfo)
{
  const auto a = base.SetTip (base.NewBlock ());
  WaitForZmqTip (a);

  /* The first call to getblockchaininfo will cache the chain.  */
  base.SetChain ("foo");
  rpc.getblockchaininfo ();
  base.SetChain ("bar");

  const auto info = rpc.getblockchaininfo ();
  EXPECT_EQ (info["chain"], "foo");
  EXPECT_EQ (info["blocks"].asInt (), a.height);
  EXPECT_EQ (info["bestblockhash"], a.hash);
}

TEST_F (ControllerRpcTests, GetBlockHashAndHeader)
{
  const auto a = base.SetTip (base.NewBlock ());
  WaitForZmqTip (a);
  const auto b = base.SetTip (base.NewBlock (genesis.hash));
  WaitForZmqTip (b);

  EXPECT_EQ (rpc.getblockhash (genesis.height), genesis.hash);
  EXPECT_EQ (rpc.getblockhash (a.height), b.hash);
  EXPECT_THROW (rpc.getblockhash (a.height + 1), jsonrpc::JsonRpcException);

  const auto hdr = rpc.getblockheader (a.hash);
  EXPECT_EQ (hdr["hash"], a.hash);
  EXPECT_EQ (hdr["height"].asInt (), a.height);
  EXPECT_THROW (rpc.getblockheader ("invalid"), jsonrpc::JsonRpcException);
}

TEST_F (ControllerRpcTests, Pending)
{
  /* We need to add a first block to get the PendingManager into synced
     state and make sure it will forward messages.  */
  const auto blk = base.SetTip (base.NewBlock ());
  WaitForZmqTip (blk);

  EXPECT_EQ (rpc.getrawmempool (), ParseJson ("[]"));

  const auto mv1 = Move ("p", "domob", "tx1", 1);
  const auto mv2 = Move ("p", "andy", "tx1", 2);
  const auto mv3 = Move ("p", "domob", "tx2", 3);

  base.AddPending ({mv1, mv2});
  base.AddPending ({mv3});

  EXPECT_EQ (rpc.getrawmempool (), ParseJson (R"(["tx1", "tx2"])"));

  EXPECT_THAT (AwaitPending (2), ElementsAre (
    ParseJson (R"(
      [
        {
          "txid": "tx1",
          "name": "domob",
          "move": 1,
          "burnt": 0
        },
        {
          "txid": "tx1",
          "name": "andy",
          "move": 2,
          "burnt": 0
        }
      ]
    )"),
    ParseJson (R"(
      [
        {
          "txid": "tx2",
          "name": "domob",
          "move": 3,
          "burnt": 0
        }
      ]
    )")
  ));
}

TEST_F (ControllerRpcTests, BaseChainErrors)
{
  /* We want to prune up to the last block (so we can test the handling
     of pruned blocks in RPC methods).  */
  Restart (0);

  base.SetTip (base.NewBlock ());
  const auto blk = base.SetTip (base.NewBlock ());
  WaitForZmqTip (blk);

  /* Cache chain and version.  */
  base.SetChain ("foo");
  base.SetVersion (42);
  rpc.getblockchaininfo ();
  rpc.getnetworkinfo ();

  base.SetShouldThrow (true);

  EXPECT_EQ (rpc.getnetworkinfo (), ParseJson (R"({
    "version": 42
  })"));

  auto res = rpc.getblockchaininfo ();
  EXPECT_EQ (res["chain"], "foo");
  EXPECT_EQ (res["blocks"].asInt (), blk.height);
  EXPECT_EQ (res["bestblockhash"], blk.hash);

  EXPECT_EQ (rpc.getblockhash (blk.height), blk.hash);
  EXPECT_THROW (rpc.getblockhash (0), jsonrpc::JsonRpcException);

  res = rpc.getblockheader (blk.hash);
  EXPECT_EQ (res["hash"], blk.hash);
  EXPECT_EQ (res["height"].asInt (), blk.height);
  EXPECT_THROW (rpc.getblockheader (genesis.hash), jsonrpc::JsonRpcException);
}

/* ************************************************************************** */

class ControllerSendUpdatesTests : public ControllerRpcTests
{

protected:

  /* genesis - b - c
             \ a
  */
  BlockData a, b, c;

  ControllerSendUpdatesTests ()
  {
    a = base.SetTip (base.NewBlock ());
    WaitForZmqTip (a);
    b = base.SetTip (base.NewBlock (genesis.hash));
    c = base.SetTip (base.NewBlock ());
    WaitForZmqTip (c);
  }

};

TEST_F (ControllerSendUpdatesTests, NoUpdates)
{
  const auto upd = rpc.game_sendupdates2 (c.hash, GAME_ID);
  EXPECT_EQ (upd["toblock"], c.hash);
  EXPECT_EQ (upd["steps"], ParseJson (R"({
    "attach": 0,
    "detach": 0
  })"));
}

TEST_F (ControllerSendUpdatesTests, AttachOnly)
{
  const auto upd = rpc.game_sendupdates2 (genesis.hash, GAME_ID);
  EXPECT_EQ (upd["toblock"], c.hash);
  EXPECT_EQ (upd["steps"], ParseJson (R"({
    "attach": 2,
    "detach": 0
  })"));
  ExpectZmq ({}, {b, c}, upd["reqtoken"].asString ());
}

TEST_F (ControllerSendUpdatesTests, DetachOnly)
{
  base.SetTip (b);
  ExpectZmq ({c}, {});

  const auto upd = rpc.game_sendupdates2 (c.hash, GAME_ID);
  EXPECT_EQ (upd["toblock"], b.hash);
  EXPECT_EQ (upd["steps"], ParseJson (R"({
    "attach": 0,
    "detach": 1
  })"));
  ExpectZmq ({c}, {}, upd["reqtoken"].asString ());
}

TEST_F (ControllerSendUpdatesTests, DetachAndAttach)
{
  const auto upd = rpc.game_sendupdates2 (a.hash, GAME_ID);
  EXPECT_EQ (upd["toblock"], c.hash);
  EXPECT_EQ (upd["steps"], ParseJson (R"({
    "attach": 2,
    "detach": 1
  })"));
  ExpectZmq ({a}, {b, c}, upd["reqtoken"].asString ());
}

TEST_F (ControllerSendUpdatesTests, ChainMismatch)
{
  /* We use an extended chain in this test:

      genesis - b - c
             |  \ - d
             \  \ - f
              \ a - e
  */

  const auto d = base.SetTip (base.NewBlock (b.hash));
  WaitForZmqTip (d);
  DisableSync ();

  /* If we reorg back to a, then the fork point according to the local
     chain state (b) will not match up with the attaches according to
     the base chain (a).  */
  base.SetTip (a);
  const auto e = base.SetTip (base.NewBlock ());

  auto upd = rpc.game_sendupdates2 (c.hash, GAME_ID);
  EXPECT_EQ (upd["toblock"], b.hash);
  EXPECT_EQ (upd["steps"], ParseJson (R"({
    "attach": 0,
    "detach": 1
  })"));
  ExpectZmq ({c}, {}, upd["reqtoken"].asString ());

  /* Build a base chain state where the fork point is b, but the newly
     attached tip is not known in the local chain at all.  */
  const auto f = base.SetTip (base.NewBlock (b.hash));
  upd = rpc.game_sendupdates2 (c.hash, GAME_ID);
  EXPECT_EQ (upd["toblock"], b.hash);
  EXPECT_EQ (upd["steps"], ParseJson (R"({
    "attach": 0,
    "detach": 1
  })"));
  ExpectZmq ({c}, {}, upd["reqtoken"].asString ());
}

TEST_F (ControllerSendUpdatesTests, UpdatesFromPrunedBlocks)
{
  /* genesis - b - c - d
            |  \ - branch0 - ... - branch4
             \ a
  */

  Restart (2);
  const auto d = base.SetTip (base.NewBlock ());
  WaitForZmqTip (d);
  const auto branch = base.AttachBranch (b.hash, 5);
  WaitForZmqTip (branch.back ());

  auto upd = rpc.game_sendupdates2 (d.hash, GAME_ID);
  EXPECT_EQ (upd["toblock"], branch.back ().hash);
  EXPECT_EQ (upd["steps"], ParseJson (R"({
    "attach": 5,
    "detach": 2
  })"));
  ExpectZmq ({d, c}, branch, upd["reqtoken"].asString ());

  upd = rpc.game_sendupdates2 (b.hash, GAME_ID);
  EXPECT_EQ (upd["toblock"], branch.back ().hash);
  EXPECT_EQ (upd["steps"], ParseJson (R"({
    "attach": 5,
    "detach": 0
  })"));
  ExpectZmq ({}, branch, upd["reqtoken"].asString ());
}

TEST_F (ControllerSendUpdatesTests, UnknownFromBlock)
{
  const auto branch = base.AttachBranch (genesis.hash, 5);
  WaitForZmqTip (branch.back ());

  const std::string invalidBlock("some unknown block");
  const auto upd = rpc.game_sendupdates2 (invalidBlock, GAME_ID);
  EXPECT_EQ (upd["toblock"], invalidBlock);
  EXPECT_TRUE (upd["error"].asBool ());
  EXPECT_EQ (upd["steps"], ParseJson (R"({
    "attach": 0,
    "detach": 0
  })"));
  ExpectZmq ({}, {}, upd["reqtoken"].asString ());
}

TEST_F (ControllerSendUpdatesTests, FutureFromBlock)
{
  DisableSync ();
  const auto branch = base.AttachBranch (genesis.hash, 5);

  const auto upd = rpc.game_sendupdates2 (branch.back ().hash, GAME_ID);
  EXPECT_EQ (upd["toblock"], branch.back ().hash);
  EXPECT_TRUE (upd["error"].asBool ());
  EXPECT_EQ (upd["steps"], ParseJson (R"({
    "attach": 0,
    "detach": 0
  })"));
  ExpectZmq ({}, {}, upd["reqtoken"].asString ());
}

TEST_F (ControllerSendUpdatesTests, ExplicitToBlock)
{
  /* ... chain0 - chain1 - chain2 - chain3 - chain4
                              \ branch
  */
  const auto chain = base.AttachBranch (c.hash, 5);
  const auto branch = base.SetTip (base.NewBlock (chain[2].hash));
  WaitForZmqTip (branch);
  base.SetTip (chain.back ());
  WaitForZmqTip (chain.back ());

  /* There are six basic scenarios to test:
     (From a branch, From the chain) x (To behind, at, after the fork point)
  */

  auto upd = rpc.game_sendupdates3 (branch.hash, GAME_ID, chain[0].hash);
  EXPECT_EQ (upd["toblock"], chain[0].hash);
  EXPECT_EQ (upd["steps"], ParseJson (R"({
    "attach": 0,
    "detach": 3
  })"));
  ExpectZmq ({branch, chain[2], chain[1]}, {}, upd["reqtoken"].asString ());

  upd = rpc.game_sendupdates3 (branch.hash, GAME_ID, chain[2].hash);
  EXPECT_EQ (upd["toblock"], chain[2].hash);
  EXPECT_EQ (upd["steps"], ParseJson (R"({
    "attach": 0,
    "detach": 1
  })"));
  ExpectZmq ({branch}, {}, upd["reqtoken"].asString ());

  upd = rpc.game_sendupdates3 (branch.hash, GAME_ID, chain[3].hash);
  EXPECT_EQ (upd["toblock"], chain[3].hash);
  EXPECT_EQ (upd["steps"], ParseJson (R"({
    "attach": 1,
    "detach": 1
  })"));
  ExpectZmq ({branch}, {chain[3]}, upd["reqtoken"].asString ());

  upd = rpc.game_sendupdates3 (chain[2].hash, GAME_ID, chain[0].hash);
  EXPECT_EQ (upd["toblock"], chain[0].hash);
  EXPECT_EQ (upd["steps"], ParseJson (R"({
    "attach": 0,
    "detach": 2
  })"));
  ExpectZmq ({chain[2], chain[1]}, {}, upd["reqtoken"].asString ());

  upd = rpc.game_sendupdates3 (chain[2].hash, GAME_ID, chain[2].hash);
  EXPECT_EQ (upd["toblock"], chain[2].hash);
  EXPECT_EQ (upd["steps"], ParseJson (R"({
    "attach": 0,
    "detach": 0
  })"));
  ExpectZmq ({}, {}, upd["reqtoken"].asString ());

  upd = rpc.game_sendupdates3 (chain[2].hash, GAME_ID, chain[3].hash);
  EXPECT_EQ (upd["toblock"], chain[3].hash);
  EXPECT_EQ (upd["steps"], ParseJson (R"({
    "attach": 1,
    "detach": 0
  })"));
  ExpectZmq ({}, {chain[3]}, upd["reqtoken"].asString ());
}

TEST_F (ControllerSendUpdatesTests, BlockRange)
{
  FLAGS_xayax_block_range = 1;

  /* ... chain0 - chain1 - chain2 - chain3 - chain4
                              \ branch0 - branch1
  */
  const auto chain = base.AttachBranch (c.hash, 5);
  const auto branch = base.AttachBranch (chain[2].hash, 2);
  WaitForZmqTip (branch.back ());
  base.SetTip (chain.back ());
  WaitForZmqTip (chain.back ());

  auto upd = rpc.game_sendupdates2 (branch[1].hash, GAME_ID);
  EXPECT_EQ (upd["toblock"], chain[3].hash);
  EXPECT_EQ (upd["steps"], ParseJson (R"({
    "attach": 1,
    "detach": 2
  })"));
  ExpectZmq ({branch[1], branch[0]}, {chain[3]}, upd["reqtoken"].asString ());

  upd = rpc.game_sendupdates3 (branch[1].hash, GAME_ID, chain[0].hash);
  EXPECT_EQ (upd["toblock"], chain[1].hash);
  EXPECT_EQ (upd["steps"], ParseJson (R"({
    "attach": 0,
    "detach": 3
  })"));
  ExpectZmq ({branch[1], branch[0], chain[2]}, {}, upd["reqtoken"].asString ());
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace xayax
