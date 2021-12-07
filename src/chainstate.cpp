// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "private/chainstate.hpp"

#include <glog/logging.h>

namespace xayax
{

namespace
{

/**
 * Sets up the schema we use for storing the chain data in the given database.
 * Does nothing if the schema is already there.
 */
void
SetupSchema (Database& db)
{
  db.Execute (R"(

    CREATE TABLE IF NOT EXISTS `blocks` (

      `hash` TEXT NOT NULL PRIMARY KEY,
      `parent` TEXT NOT NULL,
      `height` INTEGER NOT NULL,

      -- The branch this block is on.  For the main chain, it is zero;
      -- for other branches, the integer indicates the branch.
      `branch` INTEGER NOT NULL,

      -- The RNG seed as hex string.
      `rngseed` TEXT NOT NULL,

      -- Extra data for the block (e.g. timestamp or RNG seed) that
      -- is just passed along to GSPs, stored as serialised JSON.
      `metadata` TEXT NULL,

      UNIQUE (`branch`, `height`)

    );

    -- Data of moves for blocks that have not been pruned yet.
    CREATE TABLE IF NOT EXISTS `moves` (

      -- Auto-assigned ID, which we mainly use to order moves within
      -- a given block.
      `id` INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,

      -- The block hash this corresponds to.
      `block` TEXT NOT NULL,

      `txid` TEXT NOT NULL,
      `ns` TEXT NOT NULL,
      `name` TEXT NOT NULL,
      `mv` TEXT NOT NULL,

      -- Burn data is stored as a serialised JSON object for
      -- simplicity.
      `burns` TEXT NOT NULL,

      `metadata` TEXT NOT NULL

    );

    CREATE INDEX IF NOT EXISTS `moves_by_block`
        ON `moves` (`block`, `id`);

    -- Base metadata variables as a general key/value store.
    CREATE TABLE IF NOT EXISTS `variables` (
      `name` TEXT NOT NULL PRIMARY KEY,
      `value` TEXT NOT NULL
    );

  )");
}

/**
 * Queries for the lowest or highest mainchain block number.
 */
int64_t
GetMainchainHeight (const Database& db, const std::string& order)
{
  auto stmt = db.PrepareRo (R"(
    SELECT `height`
      FROM `blocks`
      WHERE `branch` = 0
      ORDER BY `height` )" + order + R"(
      LIMIT 1
  )");

  if (!stmt.Step ())
    return -1;

  const auto res = stmt.Get<int64_t> (0);
  CHECK (!stmt.Step ());

  return res;
}

/**
 * Inserts a block into the database.
 */
void
InsertBlock (Database& db, const BlockData& blk, const uint64_t branch)
{
  auto stmt = db.Prepare (R"(
    INSERT INTO `blocks`
      (`hash`, `parent`, `height`, `branch`, `rngseed`, `metadata`)
      VALUES (?1, ?2, ?3, ?4, ?5, ?6)
  )");
  stmt.Bind (1, blk.hash);
  stmt.Bind (2, blk.parent);
  stmt.Bind (3, blk.height);
  stmt.Bind (4, branch);
  stmt.Bind (5, blk.rngseed);
  stmt.Bind (6, blk.metadata);
  stmt.Execute ();

  for (const auto& m : blk.moves)
    {
      Json::Value burnsJson(Json::objectValue);
      for (const auto& entry : m.burns)
        burnsJson[entry.first] = entry.second;

      auto ins = db.Prepare (R"(
        INSERT INTO `moves`
          (`block`, `txid`, `ns`, `name`, `mv`, `burns`, `metadata`)
          VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)
      )");
      ins.Bind (1, blk.hash);
      ins.Bind (2, m.txid);
      ins.Bind (3, m.ns);
      ins.Bind (4, m.name);
      ins.Bind (5, m.mv);
      ins.Bind (6, burnsJson);
      ins.Bind (7, m.metadata);
      ins.Execute ();
    }
}

/**
 * Looks for a free branch number to use for a new branch.
 */
uint64_t
GetFreeBranchNumber (const Database& db)
{
  auto stmt = db.PrepareRo (R"(
    SELECT `branch`
      FROM `blocks`
      ORDER BY `branch` DESC
      LIMIT 1
  )");

  if (!stmt.Step ())
    {
      /* We have no blocks, every number is fine.  */
      return 1;
    }

  const auto highestBranch = stmt.Get<uint64_t> (0);
  CHECK (!stmt.Step ());

  return highestBranch + 1;
}

/**
 * Marks the given block as current tip, assuming it already exists.
 */
void
MarkAsTip (const Chainstate& s, Database& db, const BlockData& blk)
{
  auto stmt = db.PrepareRo (R"(
    SELECT `branch`
      FROM `blocks`
      WHERE `hash` = ?1
  )");
  stmt.Bind (1, blk.hash);
  CHECK (stmt.Step ()) << "Block " << blk.hash << " does not yet exist";
  const auto oldBranch = stmt.Get<uint64_t> (0);
  CHECK (!stmt.Step ());

  if (oldBranch == 0)
    {
      /* The new tip is already on the main chain.  Mark all following
         blocks (if there are any) as on a branch, at least for now until
         more of them get set as tip, too.  */
      auto upd = db.Prepare (R"(
        UPDATE `blocks`
          SET `branch` = ?1
          WHERE `branch` = 0 AND `height` > ?2
      )");
      upd.Bind (1, GetFreeBranchNumber (db));
      upd.Bind (2, blk.height);
      upd.Execute ();
    }
  else
    {
      /* The new tip is on a branch.  Look for the fork point, mark
         all blocks on the old main chain beyond the fork point as on
         a new branch, and all blocks on the new main chain until the
         current tip as branch zero.  */

      std::vector<BlockData> branch;
      CHECK (s.GetForkBranch (blk.hash, branch))
          << "Failed to get fork branch for new tip " << blk.hash;
      CHECK (!branch.empty ());

      auto upd = db.Prepare (R"(
        UPDATE `blocks`
          SET `branch` = ?1
          WHERE `branch` = 0 AND `height` >= ?2
      )");
      upd.Bind (1, GetFreeBranchNumber (db));
      upd.Bind (2, branch.rbegin ()->height);
      upd.Execute ();

      for (const auto& d : branch)
        {
          auto upd2 = db.Prepare (R"(
            UPDATE `blocks`
              SET `branch` = 0
              WHERE `hash` = ?1
          )");
          upd2.Bind (1, d.hash);
          upd2.Execute ();
        }
    }
}

} // anonymous namespace

Chainstate::Chainstate (const std::string& file)
  : Database(file)
{
  SetupSchema (*this);
}

void
Chainstate::SetChain (const std::string& chain)
{
  auto stmt = PrepareRo (R"(
    SELECT `value`
      FROM `variables`
      WHERE `name` = 'chain'
  )");

  if (stmt.Step ())
    {
      CHECK_EQ (chain, stmt.Get<std::string> (0))
          << "Chain mismatch between connected base chain and the local state";
      CHECK (!stmt.Step ());
      return;
    }

  stmt = Prepare (R"(
    INSERT INTO `variables`
      (`name`, `value`)
      VALUES ('chain', ?1)
  )");
  stmt.Bind (1, chain);
  stmt.Execute ();
}

int64_t
Chainstate::GetTipHeight () const
{
  return GetMainchainHeight (*this, "DESC");
}

int64_t
Chainstate::GetLowestUnprunedHeight () const
{
  return GetMainchainHeight (*this, "ASC");
}

bool
Chainstate::GetHashForHeight (const uint64_t height, std::string& hash) const
{
  auto stmt = PrepareRo (R"(
    SELECT `hash`
      FROM `blocks`
      WHERE `branch` = 0 AND `height` = ?1
  )");
  stmt.Bind (1, height);

  if (!stmt.Step ())
    return false;

  hash = stmt.Get<std::string> (0);
  CHECK (!stmt.Step ());

  return true;
}

bool
Chainstate::GetHeightForHash (const std::string& hash, uint64_t& height) const
{
  auto stmt = PrepareRo (R"(
    SELECT `height`
      FROM `blocks`
      WHERE `hash` = ?1
  )");
  stmt.Bind (1, hash);

  if (!stmt.Step ())
    return false;

  height = stmt.Get<uint64_t> (0);
  CHECK (!stmt.Step ());

  return true;
}

void
Chainstate::Initialise (const BlockData& genesis)
{
  LOG (INFO)
      << "Initialising chainstate with genesis block "
      << genesis.hash << " at height " << genesis.height;

  Execute (R"(
    DROP TABLE `blocks`;
    DROP TABLE `moves`;
  )");
  SetupSchema (*this);

  UpdateBatch upd(*this);
  InsertBlock (*this, genesis, 0);
  upd.Commit ();
}

bool
Chainstate::SetTip (const BlockData& blk, std::string& oldTip)
{
  /* Set the old tip from what is currently the highest branch-zero block.
     If there is none, it means we have no blocks and can't attach our tip.  */
  auto stmt = PrepareRo (R"(
    SELECT `hash`
      FROM `blocks`
      WHERE `branch` = 0
      ORDER BY `height` DESC
      LIMIT 1
  )");
  if (!stmt.Step ())
    {
      LOG (WARNING) << "We have no blocks, can't attach new tip " << blk.hash;
      return false;
    }
  oldTip = stmt.Get<std::string> (0);
  CHECK (!stmt.Step ());

  /* See if we already have the block.  If we do, check that it matches
     the main data we have now, and mark the respective chain as active.  */
  stmt = PrepareRo (R"(
    SELECT `parent`, `height`
      FROM `blocks`
      WHERE `hash` = ?1
  )");
  stmt.Bind (1, blk.hash);
  if (stmt.Step ())
    {
      LOG (INFO)
          << "We already have block " << blk.hash
          << ", marking as new tip";

      CHECK_EQ (blk.parent, stmt.Get<std::string> (0));
      CHECK_EQ (blk.height, stmt.Get<uint64_t> (1));
      CHECK (!stmt.Step ());

      UpdateBatch upd(*this);
      MarkAsTip (*this, *this, blk);
      upd.Commit ();
      return true;
    }

  /* Check the parent block.  If it does not exist, we cannot attach the new
     block to our chainstate.  If it does exist, we can attach the block
     to the parent as a temporary new branch, and then mark it as tip.  */
  stmt = PrepareRo (R"(
    SELECT `height`
      FROM `blocks`
      WHERE `hash` = ?1
  )");
  stmt.Bind (1, blk.parent);
  if (!stmt.Step ())
    {
      LOG (WARNING)
          << "Cannot attach tip " << blk.hash
          << ", parent block " << blk.parent << " is unknown";
      return false;
    }
  CHECK_EQ (blk.height, stmt.Get<uint64_t> (0) + 1)
      << "Height mismatch for new block " << blk.hash
      << " with parent " << blk.parent;
  CHECK (!stmt.Step ());

  LOG (INFO)
      << "Attaching block " << blk.hash << " to " << blk.parent
      << " as the new tip at height " << blk.height;

  UpdateBatch upd(*this);
  InsertBlock (*this, blk, GetFreeBranchNumber (*this));
  MarkAsTip (*this, *this, blk);
  upd.Commit ();

  return true;
}

bool
Chainstate::GetForkBranch (const std::string& hash,
                           std::vector<BlockData>& branch) const
{
  branch.clear ();

  /* Add all blocks on the branch of "hash" below it with decreasing height
     to the output data.  When we reach the end of the branch, use the parent
     block as new starting point and continue based on that block as well,
     until we reach the main chain.  */
  std::string curHash = hash;
  while (true)
    {
      auto stmt = PrepareRo (R"(
        SELECT `branch`, `height`
          FROM `blocks`
          WHERE `hash` = ?1
      )");
      stmt.Bind (1, curHash);

      if (!stmt.Step ())
        {
          /* The block is not known.  This can mean one of two things:
             First, if this was the initial request, it simply means that
             we do not know that block and cannot respond.  Second, if this
             is the parent of a previous branch, it could be that we reached
             the main chain but those blocks have been pruned already.  */
          return !branch.empty ();
        }

      const auto curBranch = stmt.Get<uint64_t> (0);
      const auto curHeight = stmt.Get<uint64_t> (1);
      CHECK (!stmt.Step ());

      if (curBranch == 0)
        return true;

      stmt = PrepareRo (R"(
        SELECT `hash`, `parent`, `height`, `rngseed`, `metadata`
          FROM `blocks`
          WHERE `branch` = ?1 AND `height` <= ?2
          ORDER BY `height` DESC
      )");
      stmt.Bind (1, curBranch);
      stmt.Bind (2, curHeight);

      while (stmt.Step ())
        {
          BlockData blk;
          blk.hash = stmt.Get<std::string> (0);
          blk.parent = stmt.Get<std::string> (1);
          blk.height = stmt.Get<uint64_t> (2);
          blk.rngseed = stmt.Get<std::string> (3);
          blk.metadata = stmt.Get<Json::Value> (4);

          auto moves = PrepareRo (R"(
            SELECT `txid`, `ns`, `name`, `mv`, `burns`, `metadata`
              FROM `moves`
              WHERE `block` = ?1
              ORDER BY `id` ASC
          )");
          moves.Bind (1, blk.hash);
          while (moves.Step ())
            {
              MoveData m;
              m.txid = moves.Get<std::string> (0);
              m.ns = moves.Get<std::string> (1);
              m.name = moves.Get<std::string> (2);
              m.mv = moves.Get<std::string> (3);
              m.metadata = moves.Get<Json::Value> (5);

              const auto burnsJson = moves.Get<Json::Value> (4);
              CHECK (burnsJson.isObject ());
              for (auto it = burnsJson.begin (); it != burnsJson.end (); ++it)
                {
                  CHECK (it.key ().isString ());
                  CHECK (m.burns.emplace (it.key ().asString (), *it).second);
                }

              blk.moves.emplace_back (std::move (m));
            }

          branch.emplace_back (std::move (blk));
        }

      curHash = branch.rbegin ()->parent;
    }
}

void
Chainstate::Prune (const uint64_t untilHeight)
{
  auto query = PrepareRo (R"(
    SELECT `hash`
      FROM `blocks`
      WHERE `branch` = 0 AND `height` <= ?1
  )");
  query.Bind (1, untilHeight);

  UpdateBatch upd(*this);

  unsigned cnt = 0;
  while (query.Step ())
    {
      ++cnt;
      const auto hash = query.Get<std::string> (0);

      auto stmt = Prepare (R"(
        DELETE FROM `moves`
          WHERE `block` = ?1
      )");
      stmt.Bind (1, hash);
      stmt.Execute ();
    }

  auto stmt = Prepare (R"(
    DELETE FROM `blocks`
      WHERE `branch` = 0 AND `height` <= ?1
  )");
  stmt.Bind (1, untilHeight);
  stmt.Execute ();

  upd.Commit ();

  LOG_IF (INFO, cnt > 0)
      << "Pruned " << cnt << " blocks until height " << untilHeight;
}

void
Chainstate::SanityCheck () const
{
  /* If there are no blocks (we have not been initialised yet), then nothing
     needs to be done.  */
  auto stmt = PrepareRo (R"(
    SELECT COUNT(*)
      FROM `blocks`
  )");
  CHECK (stmt.Step ());
  const unsigned numBlocks = stmt.Get<uint64_t> (0);
  CHECK (!stmt.Step ());
  if (numBlocks == 0)
    {
      LOG (INFO) << "No blocks are in the database, all good";
      return;
    }
  LOG (INFO)
      << "Running sanity check with " << numBlocks << " blocks in the database";

  /* All branches should have continguous heights, chaining back either
     to a missing block on branch zero (after the genesis) or a block
     of another branch.  */
  auto branches = PrepareRo (R"(
    SELECT DISTINCT `branch`
      FROM `blocks`
  )");
  bool foundMain = false;
  while (branches.Step ())
    {
      const auto branch = branches.Get<uint64_t> (0);
      if (branch == 0)
        foundMain = true;

      stmt = PrepareRo (R"(
        SELECT `hash`, `parent`, `height`
          FROM `blocks`
          WHERE `branch` = ?1
          ORDER BY `height` DESC
      )");
      stmt.Bind (1, branch);

      int64_t lastHeight = -1;
      std::string expectedParent;

      while (stmt.Step ())
        {
          const auto hash = stmt.Get<std::string> (0);
          const auto parent = stmt.Get<std::string> (1);
          const int64_t height = stmt.Get<uint64_t> (2);

          if (lastHeight != -1)
            {
              CHECK_EQ (height, lastHeight - 1)
                  << "Block " << hash << " has invalid height";
              CHECK_EQ (hash, expectedParent)
                  << "Block " << hash
                  << " does not match its successor's parent "
                  << expectedParent;
            }

          lastHeight = height;
          expectedParent = parent;
        }

      /* If this was branch zero, it should end at an unknown / non-existant
         block.  If this was another branch, it should end at a block of
         a different branch, or at a pruned block (missing and before the
         last unpruned height).  */
      stmt = PrepareRo (R"(
        SELECT `branch`, `height`
          FROM `blocks`
          WHERE `hash` = ?1
      )");
      stmt.Bind (1, expectedParent);

      if (branch == 0)
        CHECK (!stmt.Step ())
            << "Main branch chains to existing block " << expectedParent;
      else if (!stmt.Step ())
        {
          CHECK_LE (lastHeight, GetLowestUnprunedHeight ())
              << "Branch " << branch
              << " chains to a non-existing block " << expectedParent
              << " that is above pruning height";
        }
      else
        {
          CHECK_NE (stmt.Get<uint64_t> (0), branch)
              << "Expected end block " << expectedParent
              << " of branch " << branch << " chains back to the same branch";
          CHECK_EQ (stmt.Get<int64_t> (1), lastHeight - 1)
              << "Height mismatch at end block " << expectedParent
              << " of branch " << branch;
          CHECK (!stmt.Step ());
        }
    }
  CHECK (foundMain) << "No main branch found";
}

Chainstate::UpdateBatch::UpdateBatch (Chainstate& p)
  : parent(p)
{
  parent.Prepare ("SAVEPOINT `update-batch`").Execute ();
}

Chainstate::UpdateBatch::~UpdateBatch ()
{
  if (committed)
    return;

  LOG (WARNING) << "Reverting failed update batch";
  parent.Prepare ("ROLLBACK TO `update-batch`").Execute ();
  parent.Prepare ("RELEASE `update-batch`").Execute ();
}

void
Chainstate::UpdateBatch::Commit ()
{
  CHECK (!committed) << "Update is already committed";
  committed = true;
  parent.Prepare ("RELEASE `update-batch`").Execute ();
}

} // namespace xayax
