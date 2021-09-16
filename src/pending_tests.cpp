// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "private/pending.hpp"

#include "private/zmqpub.hpp"
#include "testutils.hpp"

#include <glog/logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <sstream>
#include <vector>

namespace xayax
{
namespace
{

using testing::ElementsAre;

/**
 * Address for the ZMQ socket in tests.  While we could use some non-TCP
 * method here for testing, using TCP is closer to what will be used in
 * production (and doesn't really hurt us much).
 */
constexpr const char* ZMQ_ADDR = "tcp://127.0.0.1:49837";

/** Game ID we use for testing.  */
const std::string GAME_ID = "game";

class PendingManagerTests : public testing::Test
{

private:

  ZmqPub pub;

protected:

  PendingManager pendings;
  TestZmqSubscriber sub;

  PendingManagerTests ()
    : pub(ZMQ_ADDR), pendings(pub), sub(ZMQ_ADDR)
  {
    pub.TrackGame (GAME_ID);

    /* Give the ZMQ publisher and subscriber some time to get connected
       before continuing with the test.  */
    SleepSome ();
  }

  ~PendingManagerTests ()
  {
    /* Sleep some time before destructing the ZMQ subscriber to make
       sure it would receive any unexpected extra messages.  */
    SleepSome ();
  }

  /**
   * Awaits n game-pending-move messages on the ZMQ subscriber, and returns
   * the associated names (which is what we use here for identifying the
   * messages we want).
   */
  std::vector<std::string>
  Receive (const size_t n)
  {
    const auto received
        = sub.AwaitMessages ("game-pending-move json " + GAME_ID, n);

    std::vector<std::string> res;
    for (const auto& msg : received)
      {
        CHECK (msg.isArray ());
        for (const auto& mv : msg)
          {
            CHECK (mv.isObject ());
            res.push_back (mv["name"].asString ());
          }
      }

    return res;
  }

  /**
   * Builds up an array move that sends some dummy data for our game and
   * with the given names.  The txid is constructed automatically from the
   * names (but won't matter anyway).
   */
  static std::vector<MoveData>
  Moves (const std::vector<std::string>& names)
  {
    std::ostringstream txid;
    for (const auto& n : names)
      txid << n << ",";

    std::vector<MoveData> res;
    for (const auto& n : names)
      {
        MoveData cur;
        cur.ns = "p";
        cur.name = n;
        cur.txid = txid.str ();
        cur.mv = R"({"g":{")" + GAME_ID + R"(":null}})";
        res.push_back (cur);
      }

    return res;
  }

};

TEST_F (PendingManagerTests, ImmediateForwarding)
{
  pendings.TipChanged ("tip");
  pendings.ChainstateTipChanged ("tip");
  pendings.PendingMoves (Moves ({"a", "b"}));
  pendings.PendingMoves (Moves ({"c"}));
  EXPECT_THAT (Receive (2), ElementsAre ("a", "b", "c"));
}

TEST_F (PendingManagerTests, WaitingForFirstTip)
{
  pendings.PendingMoves (Moves ({"a"}));
  pendings.TipChanged ("tip");
  pendings.ChainstateTipChanged ("tip");
  pendings.PendingMoves (Moves ({"b"}));
  EXPECT_THAT (Receive (1), ElementsAre ("b"));
}

TEST_F (PendingManagerTests, NotificationsCatchingUp)
{
  pendings.ChainstateTipChanged ("new");
  pendings.TipChanged ("old");
  pendings.PendingMoves (Moves ({"a"}));
  pendings.TipChanged ("new");
  pendings.PendingMoves (Moves ({"b"}));
  EXPECT_THAT (Receive (1), ElementsAre ("b"));
}

TEST_F (PendingManagerTests, TipCatchingUp)
{
  pendings.ChainstateTipChanged ("old");
  pendings.TipChanged ("new");
  pendings.PendingMoves (Moves ({"a", "b"}));
  pendings.PendingMoves (Moves ({"c"}));
  pendings.ChainstateTipChanged ("new");
  EXPECT_THAT (Receive (2), ElementsAre ("a", "b", "c"));
}

} // anonymous namespace
} // namespace xayax
