// Copyright (C) 2023-2024 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockcache.hpp"

#include "testutils.hpp"

#include <mypp/tempdb.hpp>

#include <glog/logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <sstream>

namespace xayax
{
namespace
{

using testing::ElementsAre;

/** Environment variable holding the connection URL for the MySQL temp db.  */
constexpr const char* TEMPDB_ENV = "MYPP_TEST_TEMPDB";

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

/* ************************************************************************** */

class InMemoryBlockStorageTests : public testing::Test
{

protected:

  InMemoryBlockStorage store;

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
  GetStoredRange (const uint64_t start, const uint64_t count) const
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
  EXPECT_EQ (chain.GetBlockRange (10, 5), GetStoredRange (10, 5));
  EXPECT_EQ (chain.GetBlockRange (20, 5), GetStoredRange (20, 5));
  EXPECT_EQ (base.GetBlockRangeCalls (), 2);

  /* These calls use the cache.  */
  EXPECT_EQ (chain.GetBlockRange (11, 2), GetStoredRange (11, 2));
  EXPECT_EQ (chain.GetBlockRange (20, 5), GetStoredRange (20, 5));
  EXPECT_EQ (base.GetBlockRangeCalls (), 2);

  /* This has blocks that are not yet cached.  */
  EXPECT_EQ (chain.GetBlockRange (15, 5), GetStoredRange (15, 5));
  EXPECT_EQ (base.GetBlockRangeCalls (), 3);

  /* Now they are cached (the full range from 10 to 24).  */
  EXPECT_EQ (chain.GetBlockRange (10, 15), GetStoredRange (10, 15));
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
      EXPECT_EQ (chain.GetBlockRange (tip - 2, 1), GetStoredRange (tip - 2, 1));
      EXPECT_EQ (chain.GetBlockRange (tip - 12, 11),
                 GetStoredRange (tip - 12, 11));
      EXPECT_EQ (base.GetBlockRangeCalls (), 2);
    }

  /* These ranges end up too close to the tip.  */
  for (unsigned i = 0; i < 2; ++i)
    {
      EXPECT_EQ (chain.GetBlockRange (tip - 1, 1), GetStoredRange (tip - 1, 1));
      EXPECT_EQ (chain.GetBlockRange (tip - 2, 2), GetStoredRange (tip - 2, 2));
      EXPECT_EQ (base.GetBlockRangeCalls (), 4 + 2 * i);
    }
}

/* ************************************************************************** */

class MySqlBlockStorageTests : public testing::Test
{

private:

  /**
   * Gets the URL to use for the temp db.
   */
  static std::string
  GetTempDbUrl ()
  {
    const char* url = std::getenv (TEMPDB_ENV);
    CHECK (url != nullptr)
        << "Please set the environment variable '" << TEMPDB_ENV << "'"
        << " to a MySQL URL for use in tests";
    return url;
  }

protected:

  mypp::TempDb db;
  std::unique_ptr<MySqlBlockStorage> store;

  MySqlBlockStorageTests ()
    : db(GetTempDbUrl ())
  {
    db.Initialise ();
    db.Get ().Execute (R"(
      CREATE TABLE `cached_blocks` (
        `height` BIGINT UNSIGNED NOT NULL PRIMARY KEY,
        `data` MEDIUMBLOB NOT NULL
      );
    )");

    std::string host, user, password, db, tbl;
    unsigned port;
    CHECK (MySqlBlockStorage::ParseUrl (
        GetTempDbUrl () + "/cached_blocks",
        host, port, user, password, db, tbl));
    store = std::make_unique<MySqlBlockStorage> (host, port, user, password,
                                                 db, tbl);
  }

};

TEST_F (MySqlBlockStorageTests, Parsing)
{
  std::string host, user, password, db, tbl;
  unsigned port;

  ASSERT_TRUE (MySqlBlockStorage::ParseUrl (
      "mysql://domob:foo:bar@example.com:123/database/table",
      host, port, user, password, db, tbl));
  EXPECT_EQ (host, "example.com");
  EXPECT_EQ (port, 123);
  EXPECT_EQ (user, "domob");
  EXPECT_EQ (password, "foo:bar");
  EXPECT_EQ (db, "database");
  EXPECT_EQ (tbl, "table");
}

TEST_F (MySqlBlockStorageTests, Storage)
{
  store->Store (GetRange (10, 30));
  EXPECT_THAT (store->GetRange (10, 2),
               ElementsAre (GetBlock (10), GetBlock (11)));
  EXPECT_THAT (store->GetRange (15, 3),
               ElementsAre (GetBlock (15), GetBlock (16), GetBlock (17)));
  EXPECT_THAT (store->GetRange (39, 1), ElementsAre (GetBlock (39)));
  EXPECT_THAT (store->GetRange (100, 2), ElementsAre ());
  EXPECT_THAT (store->GetRange (8, 4),
               ElementsAre (GetBlock (10), GetBlock (11)));
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace xayax
