// Copyright (C) 2023 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockcache.hpp"

#include "testutils.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <sstream>

namespace xayax
{
namespace
{

using testing::ElementsAre;

/* ************************************************************************** */

class InMemoryBlockStorageTests : public testing::Test
{

protected:

  InMemoryBlockStorage store;

  /**
   * Produces a BlockData instance for use in testing, which has the given
   * height and a fake "hash" produced from the height.
   */
  static BlockData
  GetBlock (const uint64_t height)
  {
    std::ostringstream hash;
    hash << "block " << height;

    BlockData res;
    res.height = height;
    res.hash = hash.str ();

    return res;
  }

  /**
   * Produces a range of consecutive blocks as per GetBlock.
   */
  static std::vector<BlockData>
  GetRange (const uint64_t start, const uint64_t count)
  {
    std::vector<BlockData> res;
    for (uint64_t h = start; h < start + count; ++h)
      res.push_back (GetBlock (h));

    return res;
  }

};

TEST_F (InMemoryBlockStorageTests, RetrieveRange)
{
  store.Store (GetRange (10, 30));
  EXPECT_THAT (store.GetRange (10, 2),
               ElementsAre (GetBlock (10), GetBlock (11)));
  EXPECT_THAT (store.GetRange (15, 3),
               ElementsAre (GetBlock (15), GetBlock (16), GetBlock (17)));
  EXPECT_THAT (store.GetRange (39, 1), ElementsAre (GetBlock (39)));
}

TEST_F (InMemoryBlockStorageTests, NotFullRangeInStore)
{
  /* Block 14 is not in the store.  */
  store.Store (GetRange (10, 4));
  store.Store (GetRange (15, 10));

  EXPECT_THAT (store.GetRange (9, 1), ElementsAre ());
  EXPECT_THAT (store.GetRange (14, 1), ElementsAre ());
  EXPECT_THAT (store.GetRange (10, 5), ElementsAre ());
  EXPECT_THAT (store.GetRange (25, 1), ElementsAre ());
}

/* ************************************************************************** */

class BlockCacheChainTests : public testing::Test
{

private:

  InMemoryBlockStorage store;

protected:

  TestBaseChain base;
  BlockCacheChain chain;

  /** Blocks on our chain.  */
  std::vector<BlockData> blocks;

  BlockCacheChainTests ()
    : chain(base, store, 2)
  {
    blocks.push_back (base.SetGenesis (base.NewGenesis (0)));
    for (unsigned i = 0; i < 100; ++i)
      blocks.push_back (base.SetTip (base.NewBlock ()));

    /* By calling GetTipHeight (as is usually done regularly by the sync
       process), we make sure the block cache knows the current tip height
       for matters of determining which blocks to cache.  */
    EXPECT_EQ (chain.GetTipHeight (), blocks.back ().height);
  }

  /**
   * Returns the "correct" range of blocks from our stored list.
   */
  std::vector<BlockData>
  GetRange (const uint64_t start, const uint64_t count) const
  {
    std::vector<BlockData> res;
    for (uint64_t h = start; res.size () < count; ++h)
      res.push_back (blocks[h]);
    return res;
  }

};

TEST_F (BlockCacheChainTests, UsesCacheWhenPossible)
{
  /* These two retrieve the blocks from the base chain and store them
     into the cache.  */
  EXPECT_EQ (chain.GetBlockRange (10, 5), GetRange (10, 5));
  EXPECT_EQ (chain.GetBlockRange (20, 5), GetRange (20, 5));
  EXPECT_EQ (base.GetBlockRangeCalls (), 2);

  /* These calls use the cache.  */
  EXPECT_EQ (chain.GetBlockRange (11, 2), GetRange (11, 2));
  EXPECT_EQ (chain.GetBlockRange (20, 5), GetRange (20, 5));
  EXPECT_EQ (base.GetBlockRangeCalls (), 2);

  /* This has blocks that are not yet cached.  */
  EXPECT_EQ (chain.GetBlockRange (15, 5), GetRange (15, 5));
  EXPECT_EQ (base.GetBlockRangeCalls (), 3);

  /* Now they are cached (the full range from 10 to 24).  */
  EXPECT_EQ (chain.GetBlockRange (10, 15), GetRange (10, 15));
  EXPECT_EQ (base.GetBlockRangeCalls (), 3);
}

TEST_F (BlockCacheChainTests, OnlyCachesAfterMinDepth)
{
  /* The cache is set up to only consider block ranges that have at least
     two more blocks confirming them.  */
  const uint64_t tip = chain.GetTipHeight ();

  /* These ranges will be cached.  */
  for (unsigned i = 0; i < 2; ++i)
    {
      EXPECT_EQ (chain.GetBlockRange (tip - 2, 1), GetRange (tip - 2, 1));
      EXPECT_EQ (chain.GetBlockRange (tip - 12, 11), GetRange (tip - 12, 11));
      EXPECT_EQ (base.GetBlockRangeCalls (), 2);
    }

  /* These ranges end up too close to the tip.  */
  for (unsigned i = 0; i < 2; ++i)
    {
      EXPECT_EQ (chain.GetBlockRange (tip - 1, 1), GetRange (tip - 1, 1));
      EXPECT_EQ (chain.GetBlockRange (tip - 2, 2), GetRange (tip - 2, 2));
      EXPECT_EQ (base.GetBlockRangeCalls (), 4 + 2 * i);
    }
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace xayax
