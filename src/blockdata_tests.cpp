// Copyright (C) 2023 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockdata.hpp"

#include "testutils.hpp"

#include <gtest/gtest.h>

namespace xayax
{
namespace
{

using BlockDataTests = testing::Test;

TEST_F (BlockDataTests, RoundTrip)
{
  BlockData blk;
  blk.hash = "block hash";
  blk.parent = "parent hash";
  blk.height = 42;
  blk.rngseed = "abcdef";
  blk.metadata = ParseJson (R"({"foo": "bar", "abc": [1, 2, 3]})");

  MoveData m;
  m.txid = "tx 1";
  m.ns = "p";
  m.name = "domob";
  m.mv = R"({"x": 123})";
  m.metadata = ParseJson (R"([1, 2, 3])");
  m.burns = {
    {"smc", ParseJson ("5.5")},
    {"tn", ParseJson ("null")},
    {"tftr", ParseJson (R"({"x": false})")},
  };
  blk.moves.push_back (m);

  m.txid = "tx 2";
  m.ns = "g";
  m.name = "smc";
  m.mv = R"({"mint": true})";
  m.metadata = ParseJson ("null");
  m.burns = {};
  blk.moves.push_back (m);

  BlockData blk2;
  blk2.Deserialise (blk.Serialise ());
  EXPECT_EQ (blk2, blk);
}

TEST_F (BlockDataTests, Invalid)
{
  BlockData blk;
  EXPECT_DEATH (blk.Deserialise ("abc"), "Failed to parse");
}

} // anonymous namespace
} // namespace xayax
