// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "private/zmqpub.hpp"

#include "testutils.hpp"

#include <glog/logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

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

class ZmqPubTests : public testing::Test
{

protected:

  ZmqPub pub;
  TestZmqSubscriber sub;

  ZmqPubTests ()
    : pub(ZMQ_ADDR), sub(ZMQ_ADDR)
  {
    /* Give the ZMQ publisher and subscriber some time to get connected
       before continuing with the test.  */
    SleepSome ();
  }

  ~ZmqPubTests ()
  {
    /* Sleep some time before destructing the ZMQ subscriber to make
       sure it would receive any unexpected extra messages.  */
    SleepSome ();
  }

  /**
   * Removes the base block data from the received messages.  Since most
   * tests don't care about them and instead are focused on moves, this
   * makes comparing to golden data easier.
   */
  static std::vector<Json::Value>
  WithoutBlock (const std::vector<Json::Value>& data)
  {
    std::vector<Json::Value> res;
    for (auto val : data)
      {
        val.removeMember ("block");
        res.push_back (std::move (val));
      }
    return res;
  }

  /**
   * Builds up the topic for a block-attach message.
   */
  static std::string
  Attach (const std::string& gameId)
  {
    return "game-block-attach json " + gameId;
  }

  /**
   * Builds up the topic for a block-detach message.
   */
  static std::string
  Detach (const std::string& gameId)
  {
    return "game-block-detach json " + gameId;
  }

  /**
   * Builds up a move-data instance from the given data.
   */
  static MoveData
  Move (const std::string& ns, const std::string& name, const std::string& txid,
        const std::string& mv)
  {
    MoveData res;
    res.ns = ns;
    res.name = name;
    res.txid = txid;
    res.mv = mv;
    return res;
  }

};

TEST_F (ZmqPubTests, BaseBlockData)
{
  BlockData blk;
  blk.hash = "abc";
  blk.parent = "def";
  blk.height = 10;
  blk.metadata = ParseJson (R"({"x": 42})");

  pub.TrackGame ("game");
  pub.SendBlockAttach (blk, "");

  blk.metadata = Json::Value ();
  pub.SendBlockDetach (blk, "");

  EXPECT_THAT (sub.AwaitMessages (Attach ("game"), 1), ElementsAre (
    ParseJson (R"(
      {
        "block":
          {
            "hash": "abc",
            "parent": "def",
            "height": 10,
            "x": 42
          },
        "admin": [],
        "moves": []
      }
    )")
  ));

  EXPECT_THAT (sub.AwaitMessages (Detach ("game"), 1), ElementsAre (
    ParseJson (R"(
      {
        "block":
          {
            "hash": "abc",
            "parent": "def",
            "height": 10
          },
        "admin": [],
        "moves": []
      }
    )")
  ));
}

TEST_F (ZmqPubTests, ReqToken)
{
  BlockData blk;

  pub.TrackGame ("game");
  pub.SendBlockAttach (blk, "");
  pub.SendBlockAttach (blk, "token");

  EXPECT_THAT (WithoutBlock (sub.AwaitMessages (Attach ("game"), 2)),
               ElementsAre (
    ParseJson (R"(
      {
        "admin": [],
        "moves": []
      }
    )"),
    ParseJson (R"(
      {
        "reqtoken": "token",
        "admin": [],
        "moves": []
      }
    )")
  ));
}

TEST_F (ZmqPubTests, MovesAndAdmin)
{
  BlockData blk;
  blk.moves.push_back (Move ("g", "game", "cmd1", R"(
    {
      "cmd": "abc",
      "g": {"game": "ignored"}
    }
  )"));
  blk.moves.push_back (Move ("p", "domob", "mv1", R"(
    {
      "g": {"game": ""},
      "cmd": "ignored"
    }
  )"));
  blk.moves.push_back (Move ("g", "game", "cmd2", R"(
    {
      "cmd": null
    }
  )"));
  blk.moves.push_back (Move ("p", "andy", "mv2", R"(
    {
      "g": {"game": [1, 2, 3]}
    }
  )"));
  blk.moves.push_back (Move ("p", "domob", "mv3", R"(
    {
      "g": {"game": null}
    }
  )"));

  pub.TrackGame ("game");
  pub.SendBlockAttach (blk, "");

  EXPECT_THAT (WithoutBlock (sub.AwaitMessages (Attach ("game"), 1)),
               ElementsAre (
    ParseJson (R"(
      {
        "admin":
          [
            {
              "txid": "cmd1",
              "cmd": "abc"
            },
            {
              "txid": "cmd2",
              "cmd": null
            }
          ],
        "moves":
          [
            {
              "txid": "mv1",
              "name": "domob",
              "move": "",
              "burnt": 0
            },
            {
              "txid": "mv2",
              "name": "andy",
              "move": [1, 2, 3],
              "burnt": 0
            },
            {
              "txid": "mv3",
              "name": "domob",
              "move": null,
              "burnt": 0
            }
          ]
      }
    )")
  ));
}

TEST_F (ZmqPubTests, MoveMetadata)
{
  BlockData blk;

  MoveData mv = Move ("g", "game", "cmd", R"(
    {
      "cmd": 42
    }
  )");
  mv.burns.emplace ("game", 5);
  mv.metadata = ParseJson (R"(
    {
      "meta": "?",
      "data": "!"
    }
  )");
  blk.moves.push_back (mv);

  mv = Move ("p", "domob", "mv", R"(
    {
      "g": {"game": "x"}
    }
  )");
  mv.burns.emplace ("foo", 42);
  mv.burns.emplace ("game", 100);
  mv.metadata = ParseJson (R"(
    {
      "meta": "?",
      "data": "!"
    }
  )");
  blk.moves.push_back (mv);

  pub.TrackGame ("game");
  pub.SendBlockAttach (blk, "");

  EXPECT_THAT (WithoutBlock (sub.AwaitMessages (Attach ("game"), 1)),
               ElementsAre (
    ParseJson (R"(
      {
        "admin":
          [
            {
              "txid": "cmd",
              "cmd": 42,
              "meta": "?",
              "data": "!"
            }
          ],
        "moves":
          [
            {
              "txid": "mv",
              "name": "domob",
              "move": "x",
              "meta": "?",
              "data": "!",
              "burnt": 100
            }
          ]
      }
    )")
  ));
}

TEST_F (ZmqPubTests, InvalidMoveStrings)
{
  BlockData blk;

  blk.moves.push_back (Move ("p", "domob", "", "invalid JSON"));
  blk.moves.push_back (Move ("p", "domob", "", "null"));
  blk.moves.push_back (Move ("p", "domob", "", R"(
    {
      "no": "move"
    }
  )"));
  blk.moves.push_back (Move ("p", "domob", "", R"(
    {
      "x": 1,
      "g": {"game": "foo"},
      "x": 2
    }
  )"));
  blk.moves.push_back (Move ("p", "domob", "", R"(
    {
      "g": {"game": "foo"},
      "g": {}
    }
  )"));
  blk.moves.push_back (Move ("p", "domob", "", R"(
    {
      "g": {"game": "foo", "game": "bar"}
    }
  )"));

  blk.moves.push_back (Move ("g", "game", "", "invalid JSON"));
  blk.moves.push_back (Move ("g", "game", "", "null"));
  blk.moves.push_back (Move ("g", "game", "", R"(
    {
      "no": "command"
    }
  )"));
  blk.moves.push_back (Move ("g", "game", "", R"(
    {
      "cmd": "foo",
      "cmd": "bar"
    }
  )"));

  pub.TrackGame ("game");
  pub.SendBlockAttach (blk, "");

  EXPECT_THAT (WithoutBlock (sub.AwaitMessages (Attach ("game"), 1)),
               ElementsAre (
    ParseJson (R"(
      {
        "admin": [],
        "moves": []
      }
    )")
  ));
}

TEST_F (ZmqPubTests, TrackedGames)
{
  const auto mv = Move ("p", "domob", "move", R"(
    {
      "cmd": "ignored",
      "g":
        {
          "foo": 42,
          "bar": {"x": true}
        }
    }
  )");
  const auto cmdFoo = Move ("g", "foo", "cmd1", R"(
    {
      "cmd": "foo cmd",
      "g": {"foo": "ignored"}
    }
  )");
  const auto cmdBar = Move ("g", "bar", "cmd2", R"(
    {
      "cmd": "bar cmd",
      "g": {"bar": "ignored"}
    }
  )");

  BlockData blk;
  blk.moves = {mv};

  pub.SendBlockAttach (blk, "");
  /* No games are tracked so far.  */

  pub.TrackGame ("foo");
  pub.SendBlockAttach (blk, "");

  pub.TrackGame ("bar");
  blk.moves = {mv, cmdFoo, cmdBar};
  pub.SendBlockAttach (blk, "");

  pub.UntrackGame ("foo");
  blk.moves = {cmdFoo, cmdBar};
  pub.SendBlockAttach (blk, "");

  EXPECT_THAT (WithoutBlock (sub.AwaitMessages (Attach ("foo"), 2)),
               ElementsAre (
    ParseJson (R"(
      {
        "admin": [],
        "moves":
          [
            {
              "txid": "move",
              "name": "domob",
              "move": 42,
              "burnt": 0
            }
          ]
      }
    )"),
    ParseJson (R"(
      {
        "admin":
          [
            {
              "txid": "cmd1",
              "cmd": "foo cmd"
            }
          ],
        "moves":
          [
            {
              "txid": "move",
              "name": "domob",
              "move": 42,
              "burnt": 0
            }
          ]
      }
    )")
  ));

  EXPECT_THAT (WithoutBlock (sub.AwaitMessages (Attach ("bar"), 2)),
               ElementsAre (
    ParseJson (R"(
      {
        "admin":
          [
            {
              "txid": "cmd2",
              "cmd": "bar cmd"
            }
          ],
        "moves":
          [
            {
              "txid": "move",
              "name": "domob",
              "move": {"x": true},
              "burnt": 0
            }
          ]
      }
    )"),
    ParseJson (R"(
      {
        "admin":
          [
            {
              "txid": "cmd2",
              "cmd": "bar cmd"
            }
          ],
        "moves": []
      }
    )")
  ));
}

} // anonymous namespace
} // namespace xayax
