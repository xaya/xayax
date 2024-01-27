// Copyright (C) 2023-2024 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockcache.hpp"

#include <mypp/connection.hpp>
#include <mypp/error.hpp>
#include <mypp/statement.hpp>
#include <mypp/url.hpp>

#include <glog/logging.h>

/* The MySQL cache stores blocks into a single table inside a given database,
   which should be set up with a schema like this:

   CREATE TABLE `cached_blocks` (
      `height` BIGINT UNSIGNED NOT NULL PRIMARY KEY,
      `data` MEDIUMBLOB NOT NULL
   );
*/

namespace xayax
{

/* ************************************************************************** */

class MySqlBlockStorage::Implementation
{

private:

  /** The underlying mypp connection.  */
  mypp::Connection connection;

  /** The name of the table to use.  */
  std::string table;

public:

  Implementation () = default;

  /**
   * Opens the connection to the MySQL server.  Returns false if the connection
   * fails.
   */
  bool Connect (const std::string& host, unsigned port,
                const std::string& user, const std::string& password,
                const std::string& db, const std::string& tbl);

  /**
   * Stores an array of blocks into the database.
   */
  void Store (const std::vector<BlockData>& blocks);

  /**
   * Retrieves a range of blocks, as far as they are in the cache.
   */
  std::vector<BlockData> GetRange (uint64_t start, uint64_t count);

};

bool
MySqlBlockStorage::Implementation::Connect (
    const std::string& host, const unsigned port,
    const std::string& user, const std::string& password,
    const std::string& db, const std::string& tbl)
{
  try
    {
      connection.Connect (host, port, user, password, db);
      table = tbl;
      LOG (INFO)
          << "Connected to MySQL server at " << host
          << " as user " << user << ", using table " << db << "." << tbl;
      return true;
    }
  catch (const mypp::Error& exc)
    {
      LOG (ERROR) << exc.what ();
      return false;
    }
}

void
MySqlBlockStorage::Implementation::Store (const std::vector<BlockData>& blocks)
{
  for (const auto& b : blocks)
    {
      mypp::Statement stmt(*connection);
      try
        {
          stmt.Prepare (2, R"(
            REPLACE INTO `)" + table + R"(`
              (`height`, `data`) VALUES (?, ?)
          )");
        }
      catch (const mypp::Error& exc)
        {
          LOG (FATAL) << exc.what ();
        }

      stmt.Bind<int64_t> (0, b.height);
      stmt.BindBlob (1, b.Serialise ());

      try
        {
          stmt.Execute ();
        }
      catch (const mypp::Error& exc)
        {
          LOG (WARNING) << exc.what ();
          /* We continue here and try the next block.  It is not fatal
             if one of them failed to insert for whatever reason.  */
        }
    }
}

std::vector<BlockData>
MySqlBlockStorage::Implementation::GetRange (const uint64_t start,
                                             const uint64_t count)
{
  mypp::Statement stmt(*connection);
  try
    {
      stmt.Prepare (2, R"(
        SELECT `data`
          FROM `)" + table + R"(`
          WHERE `height` >= ? AND `height` < ?
          ORDER BY `height` ASC
      )");
    }
  catch (const mypp::Error& exc)
    {
      LOG (FATAL) << exc.what ();
    }

  stmt.Bind<int64_t> (0, start);
  stmt.Bind<int64_t> (1, start + count);

  try
    {
      stmt.Query ();

      std::vector<BlockData> res;
      while (stmt.Fetch ())
        {
          res.emplace_back ();
          res.back ().Deserialise (stmt.GetBlob ("data"));
        }

      return res;
    }
  catch (const mypp::Error& exc)
    {
      LOG (WARNING) << exc.what ();
      return {};
    }
}

/* ************************************************************************** */

MySqlBlockStorage::MySqlBlockStorage (
    const std::string& host, const unsigned port,
    const std::string& user, const std::string& password,
    const std::string& db, const std::string& tbl)
{
  impl = std::make_unique<Implementation> ();
  CHECK (impl->Connect (host, port, user, password, db, tbl))
      << "Failed to make MySQL connection";
}

MySqlBlockStorage::~MySqlBlockStorage () = default;

void
MySqlBlockStorage::Store (const std::vector<BlockData>& blocks)
{
  impl->Store (blocks);
}

std::vector<BlockData>
MySqlBlockStorage::GetRange (const uint64_t start, const uint64_t count)
{
  return impl->GetRange (start, count);
}

bool
MySqlBlockStorage::ParseUrl (const std::string& url,  
                             std::string& host, unsigned& port,
                             std::string& user, std::string& password,
                             std::string& db, std::string& tbl)
{
  try
    {
      mypp::ParseUrl (url, host, port, user, password, db, tbl);
      if (tbl.empty ())
        return false;
      return true;
    }
  catch (const mypp::Error& exc)
    {
      LOG (ERROR) << exc.what ();
      return false;
    }
}

/* ************************************************************************** */

} // namespace xayax
