// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockdata.hpp"
#include "private/chainstate.hpp"
#include "testutils.hpp"

#include <glog/logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <map>
#include <sstream>

namespace xayax
{
namespace
{

using testing::ElementsAre;

/* ************************************************************************** */

/**
 * Test fixture for Chainstate.  It has a Chainstate instance that is being
 * tested (with an in-memory database), and also utility methods for generating
 * and attaching blocks easily.
 */
class ChainstateTests : public testing::Test
{

private:

  /**
   * Block hashes are generated automatically, simply based on a counter
   * that is incremented and written as text whenever a new fake block is
   * generated.  This is the counter.
   */
  unsigned hashCounter = 0;

  /**
   * Generates and returns the next block hash.
   */
  std::string
  NextHash ()
  {
    ++hashCounter;

    std::ostringstream res;
    res << "block " << hashCounter;

    return res.str ();
  }

protected:

  /** Our own map of blocks (including in particular their height).  */
  std::map<std::string, BlockData> blocks;

  Chainstate state;

  ChainstateTests ()
    : state(":memory:")
  {}

  ~ChainstateTests ()
  {
    state.SanityCheck ();
  }

  /**
   * Generates a new genesis block with the given height, sets it
   * with Initialise and returns the block's hash.
   */
  std::string
  SetGenesis (const uint64_t height)
  {
    BlockData res;
    res.hash = NextHash ();
    res.parent = "pregenesis";
    res.height = height;

    blocks.emplace (res.hash, res);

    state.Initialise (res);

    return res.hash;
  }

  /**
   * Builds a new block that chains to the given parent and returns the
   * raw data as reference inside our block map.  The block is not
   * attached to the chainstate yet.
   */
  BlockData&
  NewBlock (const std::string& parent)
  {
    BlockData blk;
    blk.hash = NextHash ();
    blk.parent = parent;
    blk.height = GetBlock (parent).height + 1;

    auto res = blocks.emplace (blk.hash, blk);
    CHECK (res.second);

    return res.first->second;
  }

  /**
   * Generates a new block that is supposed to be attached to the parent
   * block with the given hash.  Attaches it as new tip in our chain state
   * and returns the block's hash.
   *
   * If attaching fails, "error" is returned instead of a block hash.
   */
  std::string
  AddBlock (const std::string& parent, std::string& oldTip)
  {
    const auto& blk = NewBlock (parent);
    if (!state.SetTip (blk, oldTip))
      return "error";

    return blk.hash;
  }

  std::string
  AddBlock (const std::string& parent)
  {
    std::string oldTip;
    return AddBlock (parent, oldTip);
  }

  /**
   * Returns the data for an already added block.
   */
  const BlockData&
  GetBlock (const std::string& hash) const
  {
    return blocks.at (hash);
  }

};

/* ************************************************************************** */

TEST_F (ChainstateTests, SetupWorks)
{}

TEST_F (ChainstateTests, SetChain)
{
  state.SetChain ("foo");
  state.SetChain ("foo");
  EXPECT_DEATH (state.SetChain ("bar"), "Chain mismatch");
}

TEST_F (ChainstateTests, NoBlocks)
{
  EXPECT_EQ (state.GetTipHeight (), -1);

  std::string hash;
  EXPECT_FALSE (state.GetHashForHeight (42, hash));

  uint64_t height;
  EXPECT_FALSE (state.GetHeightForHash ("block", height));
}

TEST_F (ChainstateTests, BasicChain)
{
  const auto genesis = SetGenesis (10);
  const auto a = AddBlock (genesis);
  const auto b = AddBlock (a);

  std::string oldTip;
  const auto c = AddBlock (b, oldTip);
  EXPECT_EQ (oldTip, b);

  EXPECT_EQ (state.GetTipHeight (), 13);

  std::string hash;
  EXPECT_FALSE (state.GetHashForHeight (9, hash));
  EXPECT_FALSE (state.GetHashForHeight (14, hash));
  ASSERT_TRUE (state.GetHashForHeight (12, hash));
  EXPECT_EQ (hash, b);

  uint64_t height;
  EXPECT_FALSE (state.GetHeightForHash ("invalid", height));
  ASSERT_TRUE (state.GetHeightForHash (c, height));
  EXPECT_EQ (height, 13);
}

TEST_F (ChainstateTests, SettingOldBlockAsTip)
{
  const auto genesis = SetGenesis (10);
  const auto a = AddBlock (genesis);
  const auto b = AddBlock (a);

  std::string oldTip;
  ASSERT_TRUE (state.SetTip (GetBlock (a), oldTip));
  EXPECT_EQ (oldTip, b);

  EXPECT_EQ (state.GetTipHeight (), 11);

  std::string hash;
  EXPECT_FALSE (state.GetHashForHeight (12, hash));
  ASSERT_TRUE (state.GetHashForHeight (11, hash));
  EXPECT_EQ (hash, a);

  uint64_t height;
  ASSERT_TRUE (state.GetHeightForHash (a, height));
  EXPECT_EQ (height, 11);
  ASSERT_TRUE (state.GetHeightForHash (b, height));
  EXPECT_EQ (height, 12);

  const auto c = AddBlock (b, oldTip);
  EXPECT_EQ (oldTip, a);
  EXPECT_EQ (state.GetTipHeight (), 13);
}

TEST_F (ChainstateTests, Reinitialisation)
{
  const auto genesis1 = SetGenesis (10);
  const auto a = AddBlock (genesis1);
  const auto b = AddBlock (a);

  const auto genesis2 = SetGenesis (11);

  EXPECT_EQ (state.GetTipHeight (), 11);

  std::string hash;
  EXPECT_FALSE (state.GetHashForHeight (10, hash));
  EXPECT_FALSE (state.GetHashForHeight (12, hash));
  ASSERT_TRUE (state.GetHashForHeight (11, hash));
  EXPECT_EQ (hash, genesis2);

  uint64_t height;
  EXPECT_FALSE (state.GetHeightForHash (genesis1, height));
  EXPECT_FALSE (state.GetHeightForHash (a, height));
  EXPECT_FALSE (state.GetHeightForHash (b, height));
  ASSERT_TRUE (state.GetHeightForHash (genesis2, height));
  EXPECT_EQ (height, 11);
}

TEST_F (ChainstateTests, InvalidAttach)
{
  /* AddBlock expects a result to be in our block map for the parent, so it can
     retrieve the height.  Fake this.  */
  BlockData fake;
  fake.height = 42;
  blocks.emplace ("invalid", fake);

  EXPECT_EQ (AddBlock ("invalid"), "error");
  SetGenesis (10);
  EXPECT_EQ (AddBlock ("invalid"), "error");
}

TEST_F (ChainstateTests, ForkedChain)
{
  /* In this test, we fork the existing chain once, and then fork it
     again below the previous fork point.  The original tip will then
     be separated from the main chain by two branches.  Make sure this
     works, and also make sure that we can then reactivate it again
     properly.

     genesis - a - b - e
            \    \ c
             - d
  */

  const auto genesis = SetGenesis (10);
  const auto a = AddBlock (genesis);
  const auto b = AddBlock (a);

  std::string oldTip;
  const auto c = AddBlock (a, oldTip);
  EXPECT_EQ (oldTip, b);

  const auto d = AddBlock (genesis, oldTip);
  EXPECT_EQ (oldTip, c);

  EXPECT_EQ (state.GetTipHeight (), 11);

  /* We should be able to query for all blocks by hash, but the orphaned
     blocks cannot be retrieved by height.  */

  std::string hash;
  EXPECT_FALSE (state.GetHashForHeight (12, hash));
  ASSERT_TRUE (state.GetHashForHeight (11, hash));
  EXPECT_EQ (hash, d);

  uint64_t height;
  ASSERT_TRUE (state.GetHeightForHash (a, height));
  EXPECT_EQ (height, 11);
  ASSERT_TRUE (state.GetHeightForHash (b, height));
  EXPECT_EQ (height, 12);
  ASSERT_TRUE (state.GetHeightForHash (c, height));
  EXPECT_EQ (height, 12);
  ASSERT_TRUE (state.GetHeightForHash (d, height));
  EXPECT_EQ (height, 11);

  /* Activate back the chain with b.  */
  const auto e = AddBlock (b, oldTip);
  EXPECT_EQ (oldTip, d);
  EXPECT_EQ (state.GetTipHeight (), 13);
  ASSERT_TRUE (state.GetHashForHeight (12, hash));
  EXPECT_EQ (hash, b);
}

TEST_F (ChainstateTests, ForkBranch)
{
  /* We build up the same situation as in the ForkedChain test, except that
     d is the active tip.  Then we use this to verify that the fork branch
     detection e.g. from e works as it should.  */

  const auto genesis = SetGenesis (10);
  const auto a = AddBlock (genesis);
  const auto b = AddBlock (a);
  const auto e = AddBlock (b);
  const auto c = AddBlock (a);
  const auto d = AddBlock (genesis);

  std::vector<BlockData> branch;
  EXPECT_FALSE (state.GetForkBranch ("invalid", branch));

  ASSERT_TRUE (state.GetForkBranch (genesis, branch));
  EXPECT_TRUE (branch.empty ());
  ASSERT_TRUE (state.GetForkBranch (d, branch));
  EXPECT_TRUE (branch.empty ());

  ASSERT_TRUE (state.GetForkBranch (e, branch));
  EXPECT_THAT (branch, ElementsAre (GetBlock (e), GetBlock (b), GetBlock (a)));
}

TEST_F (ChainstateTests, ExtraDataAndPruning)
{
  const auto genesis = SetGenesis (10);

  auto* blk = &NewBlock (genesis);
  blk->metadata = ParseJson (R"({
    "foo": "bar",
    "abc": 42,
    "true": false
  })");

  MoveData m;
  m.txid = "txid";
  m.ns = "p";
  m.name = "domob";
  m.mv = "foo";
  m.metadata = ParseJson (R"({"a": "b"})");
  blk->moves.push_back (m);
  m.mv = "bar";
  m.metadata = "x";
  blk->moves.push_back (m);

  const std::string a = blk->hash;

  std::string oldTip;
  ASSERT_TRUE (state.SetTip (*blk, oldTip));

  blk = &NewBlock (a);
  blk->rngseed = "00aabb";
  blk->metadata = 100;
  m.name = "andy";
  m.burns.emplace ("tn", 1.25);
  m.burns.emplace ("xs", "100");
  blk->moves.push_back (m);
  const std::string b = blk->hash;
  ASSERT_TRUE (state.SetTip (*blk, oldTip));

  /* Add a long, alternate chain.  */
  std::string cur = genesis;
  for (unsigned i = 0; i < 20; ++i)
    cur = AddBlock (cur);

  /* Prune up to the tip (excluding only the last block).  */
  state.Prune (state.GetTipHeight () - 1);

  /* Retrieving the fork branch for the two orphan blocks should still work,
     including all the data.  */
  std::vector<BlockData> branch;
  ASSERT_TRUE (state.GetForkBranch (b, branch));
  EXPECT_THAT (branch, ElementsAre (GetBlock (b), GetBlock (a)));

  /* Forking the very last block is fine.  */
  std::string hash;
  ASSERT_TRUE (state.GetHashForHeight (state.GetTipHeight () - 1, hash));
  AddBlock (hash);
  ASSERT_TRUE (state.GetForkBranch (cur, branch));

  /* Forking a block further down aborts as we have already pruned
     those detached blocks.  */
  ASSERT_TRUE (state.GetHashForHeight (state.GetTipHeight () - 2, hash));
  EXPECT_DEATH (
    {
      /* We need to do the AddBlock also here inside the EXPECT_DEATH
         macro, or else the end-of-test sanity check will die as well.  */
      AddBlock (hash);
      state.GetForkBranch (cur, branch);
    }, "is already pruned");
}

TEST_F (ChainstateTests, UpdateBatch)
{
  Chainstate::UpdateBatch outer(state);
  const auto genesis = SetGenesis (10);

  std::string a;
  {
    Chainstate::UpdateBatch inner(state);
    a = AddBlock (genesis);
    inner.Commit ();
  }

  uint64_t height;
  ASSERT_TRUE (state.GetHeightForHash (genesis, height));
  ASSERT_TRUE (state.GetHeightForHash (a, height));

  std::string b;
  {
    Chainstate::UpdateBatch inner(state);
    b = AddBlock (genesis);
    ASSERT_TRUE (state.GetHeightForHash (b, height));
    /* Let the batch revert.  */
  }
  ASSERT_FALSE (state.GetHeightForHash (b, height));

  outer.Commit ();

  ASSERT_TRUE (state.GetHeightForHash (genesis, height));
  ASSERT_TRUE (state.GetHeightForHash (a, height));
  ASSERT_FALSE (state.GetHeightForHash (b, height));
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace xayax
