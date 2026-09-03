// Copyright (C) 2023-2026 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockcache.hpp"

#include <mypp/connection.hpp>
#include <mypp/error.hpp>
#include <mypp/statement.hpp>
#include <mypp/url.hpp>

#include <glog/logging.h>

#include <memory>
#include <mutex>

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

  /**
   * The underlying mypp connection.  It is replaced by a fresh one when
   * the server side went away (idle reap, server restart, network
   * failure).
   */
  std::unique_ptr<mypp::Connection> connection;

  /* Connect parameters, kept so that the connection can be rebuilt.  */
  std::string host;
  unsigned port = 0;
  std::string user;
  std::string password;
  std::string db;

  /* Client certificate paths (all empty if none is used).  */
  std::string sslCa;
  std::string sslCert;
  std::string sslKey;

  /**
   * Mutex serialising all access to the handle.  The RPC server calls
   * GetBlockRange from several worker threads, and a MYSQL handle must
   * not be used concurrently.
   */
  std::mutex mut;

  /** The name of the table to use.  */
  std::string table;

  /**
   * Opens a fresh connection with the stored parameters, replacing any
   * existing one.  Returns false (after logging) if that fails.  Must be
   * called with mut held.
   */
  bool Reconnect ();

  /**
   * Returns true if the current connection is gone (or there is none),
   * i.e. a failed operation should be retried on a fresh one.  Must be
   * called with mut held.
   */
  bool ConnectionLost ();

  /**
   * Runs op on the current connection, opening one first if there is
   * none.  If op throws a mypp::Error because the connection is gone,
   * a fresh connection is opened and op is run once more; any other
   * error (or a second loss) is passed on to the caller.  If no
   * connection can be opened, op is not run.  Must be called with mut
   * held.
   */
  template <typename Op>
    void WithConnection (const std::string& what, Op&& op);

public:

  Implementation () = default;

  /**
   * Enables a client certificate.
   */
  void UseCert (const std::string& ca, const std::string& cert,
                const std::string& key);

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

void
MySqlBlockStorage::Implementation::UseCert (const std::string& ca,
                                            const std::string& cert,
                                            const std::string& key)
{
  sslCa = ca;
  sslCert = cert;
  sslKey = key;
}

bool
MySqlBlockStorage::Implementation::Connect (
    const std::string& h, const unsigned p,
    const std::string& u, const std::string& pw,
    const std::string& d, const std::string& tbl)
{
  host = h;
  port = p;
  user = u;
  password = pw;
  db = d;
  table = tbl;

  std::lock_guard<std::mutex> lock(mut);
  return Reconnect ();
}

bool
MySqlBlockStorage::Implementation::Reconnect ()
{
  connection = std::make_unique<mypp::Connection> ();
  if (!sslCert.empty ())
    connection->UseClientCertificate (sslCa, sslCert, sslKey);

  try
    {
      connection->Connect (host, port, user, password, db);
      LOG (INFO)
          << "Connected to MySQL server at " << host
          << " as user " << user << ", using table " << db << "." << table;
      return true;
    }
  catch (const mypp::Error& exc)
    {
      LOG (ERROR) << exc.what ();
      connection.reset ();
      return false;
    }
}

bool
MySqlBlockStorage::Implementation::ConnectionLost ()
{
  if (connection == nullptr || !*connection)
    return true;
  /* Without MYSQL_OPT_RECONNECT, mysql_ping fails exactly when the server
     side of this connection is gone, whatever operation noticed first.  */
  return mysql_ping (**connection) != 0;
}

template <typename Op>
  void
  MySqlBlockStorage::Implementation::WithConnection (const std::string& what,
                                                     Op&& op)
{
  for (int attempt = 0; attempt < 2; ++attempt)
    {
      if (connection == nullptr && !Reconnect ())
        return;

      try
        {
          op (*connection);
          return;
        }
      catch (const mypp::Error& exc)
        {
          if (attempt > 0 || !ConnectionLost ())
            throw;
          LOG (WARNING)
              << "MySQL connection lost while " << what
              << ", reconnecting: " << exc.what ();
          connection.reset ();
        }
    }
}

void
MySqlBlockStorage::Implementation::Store (const std::vector<BlockData>& blocks)
{
  std::lock_guard<std::mutex> lock(mut);

  try
    {
      WithConnection ("storing blocks", [&] (mypp::Connection& c)
        {
          mypp::Statement stmt(*c);
          stmt.Prepare (2, R"(
            REPLACE INTO `)" + table + R"(`
              (`height`, `data`) VALUES (?, ?)
          )");

          for (const auto& b : blocks)
            {
              try
                {
                  stmt.Reset ();
                  stmt.Bind<int64_t> (0, b.height);
                  stmt.BindBlob (1, b.Serialise ());
                  stmt.Execute ();
                }
              catch (const mypp::Error& exc)
                {
                  /* A lost connection is handled by WithConnection.  */
                  if (ConnectionLost ())
                    throw;

                  LOG (WARNING)
                      << "Failed to insert into block cache: " << exc.what ();
                  /* We continue here and try the next block.  It is not
                     fatal if one of them failed to insert for whatever
                     reason.  */
                }
            }
        });
    }
  catch (const mypp::Error& exc)
    {
      LOG (WARNING) << "Failed to store into block cache: " << exc.what ();
    }
}

std::vector<BlockData>
MySqlBlockStorage::Implementation::GetRange (const uint64_t start,
                                             const uint64_t count)
{
  std::lock_guard<std::mutex> lock(mut);

  std::vector<BlockData> res;
  try
    {
      WithConnection ("reading the block cache", [&] (mypp::Connection& c)
        {
          mypp::Statement stmt(*c);
          stmt.Prepare (2, R"(
            SELECT `data`
              FROM `)" + table + R"(`
              WHERE `height` >= ? AND `height` < ?
              ORDER BY `height` ASC
          )");

          stmt.Bind<int64_t> (0, start);
          stmt.Bind<int64_t> (1, start + count);

          stmt.Query ();

          res.clear ();
          while (stmt.Fetch ())
            {
              res.emplace_back ();
              res.back ().Deserialise (stmt.GetBlob ("data"));
            }
        });
    }
  catch (const mypp::Error& exc)
    {
      LOG (WARNING)
          << "Failed to retrieve data from block cache: " << exc.what ();
      return {};
    }

  return res;
}

/* ************************************************************************** */

MySqlBlockStorage::MySqlBlockStorage () = default;
MySqlBlockStorage::~MySqlBlockStorage () = default;

bool
MySqlBlockStorage::Connect (const std::string& url)
{
  CHECK (impl == nullptr) << "MySqlBlockStorage is already connected";

  mypp::UrlParser parser;
  try
    {
      parser.Parse (url);
    }
  catch (const mypp::Error& exc)
    {
      LOG (ERROR) << exc.what ();
      return false;
    }

  if (!parser.HasTable ())
    {
      LOG (ERROR) << "Provided URL has no table specified";
      return false;
    }

  impl = std::make_unique<Implementation> ();

  if (parser.HasOption ("ssl-cert"))
    {
      LOG (INFO) << "Using client certificate for MySQL connection";
      impl->UseCert (parser.GetOption ("ssl-ca"),
                     parser.GetOption ("ssl-cert"),
                     parser.GetOption ("ssl-key"));
    }

  if (!impl->Connect (parser.GetHost (), parser.GetPort (),
                      parser.GetUser (), parser.GetPassword (),
                      parser.GetDatabase (), parser.GetTable ()))
    {
      LOG (ERROR) << "Failed to make MySQL connection";
      impl.reset ();
      return false;
    }

  return true;
}

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

/* ************************************************************************** */

} // namespace xayax
