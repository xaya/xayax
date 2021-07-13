// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "private/database.hpp"

#include <json/json.h>

#include <glog/logging.h>

#include <limits>
#include <sstream>

namespace xayax
{

/* ************************************************************************** */

namespace
{

/**
 * Error callback for SQLite, which prints logs using glog.
 */
void
SQLiteErrorLogger (void* arg, const int errCode, const char* msg)
{
  LOG (ERROR) << "SQLite error (code " << errCode << "): " << msg;
}

} // anonymous namespace

Database::Database (const std::string& file)
  : db(nullptr)
{
  static bool initialised = false;

  if (!initialised)
    {
      LOG (INFO)
          << "Using SQLite version " << SQLITE_VERSION
          << " (library version: " << sqlite3_libversion () << ")";
      CHECK_EQ (SQLITE_VERSION_NUMBER, sqlite3_libversion_number ())
          << "Mismatch between header and library SQLite versions";

      const int rc
          = sqlite3_config (SQLITE_CONFIG_LOG, &SQLiteErrorLogger, nullptr);
      if (rc != SQLITE_OK)
        LOG (WARNING) << "Failed to set up SQLite error handler: " << rc;
      else
        LOG (INFO) << "Configured SQLite error handler";

      CHECK_EQ (sqlite3_config (SQLITE_CONFIG_MULTITHREAD, nullptr), SQLITE_OK)
          << "Failed to enable multi-threaded mode for SQLite";

      initialised = true;
    }

  const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
  const int rc = sqlite3_open_v2 (file.c_str (), &db, flags, nullptr);
  if (rc != SQLITE_OK)
    LOG (FATAL) << "Failed to open SQLite database: " << file;

  CHECK (db != nullptr);
  LOG (INFO) << "Opened SQLite database successfully: " << file;
}

Database::~Database ()
{
  statements.clear ();

  CHECK (db != nullptr);
  CHECK_EQ (sqlite3_close (db), SQLITE_OK) << "Failed to close SQLite database";
}

namespace
{

/**
 * Callback for sqlite3_exec that expects not to be called.
 */
int
ExpectNoResult (void* data, int columns, char** strs, char** names)
{
  LOG (FATAL) << "Expected no result from DB query";
}

} // anonymous namespace

void
Database::Execute (const std::string& sql)
{
  CHECK_EQ (sqlite3_exec (db, sql.c_str (), &ExpectNoResult,
                          nullptr, nullptr),
            SQLITE_OK);
}

Database::Statement
Database::Prepare (const std::string& sql)
{
  return PrepareRo (sql);
}

Database::Statement
Database::PrepareRo (const std::string& sql) const
{
  CHECK (db != nullptr);

  const auto mit = statements.find (sql);
  if (mit != statements.end ())
    return Statement (*mit->second);

  sqlite3_stmt* stmt = nullptr;
  CHECK_EQ (sqlite3_prepare_v2 (db, sql.c_str (), sql.size () + 1,
                                &stmt, nullptr),
            SQLITE_OK)
      << "Failed to prepare SQL statement";

  auto entry = std::make_unique<CachedStatement> (stmt);
  Statement res(*entry);

  VLOG (2)
      << "Created new SQL statement cache entry " << entry.get ()
      << " for:\n" << sql;
  statements.emplace (sql, std::move (entry));

  return res;
}

/* ************************************************************************** */

Database::CachedStatement::~CachedStatement ()
{
  CHECK (!used) << "Cached statement is still in use";

  /* sqlite3_finalize returns the error code corresponding to the last
     evaluation of the statement, not an error code "about" finalising it.
     Thus we want to ignore it here.  */
  sqlite3_finalize (stmt);
}

void
Database::CachedStatement::Acquire ()
{
  CHECK (!used) << "Cached statement is already in use";
  used = true;
}

void
Database::CachedStatement::Release ()
{
  CHECK (used) << "Cached statement is not in use";
  used = false;

  CHECK_EQ (sqlite3_clear_bindings (stmt), SQLITE_OK);
  /* sqlite3_reset returns an error code if the last execution of the
     statement had an error.  We don't care about that here.  */
  sqlite3_reset (stmt);
}

/* ************************************************************************** */

Database::Statement::Statement (CachedStatement& s)
  : entry(&s)
{
  s.Acquire ();
}

Database::Statement::Statement (Statement&& o)
{
  *this = std::move (o);
}

Database::Statement&
Database::Statement::operator= (Statement&& o)
{
  Clear ();

  entry = o.entry;
  o.entry = nullptr;

  return *this;
}

Database::Statement::~Statement ()
{
  Clear ();
}

void
Database::Statement::Clear ()
{
  if (entry != nullptr)
    {
      entry->Release ();
      entry = nullptr;
    }
}

void
Database::Statement::Execute ()
{
  CHECK (!Step ());
}

sqlite3_stmt*
Database::Statement::operator* () const
{
  CHECK (entry != nullptr) << "Statement is empty";
  return entry->stmt;
}

bool
Database::Statement::Step ()
{
  const int rc = sqlite3_step (**this);
  switch (rc)
    {
    case SQLITE_ROW:
      return true;
    case SQLITE_DONE:
      return false;
    default:
      LOG (FATAL) << "Unexpected SQLite step result: " << rc;
    }
}

void
Database::Statement::BindNull (const int ind)
{
  CHECK_EQ (sqlite3_bind_null (**this, ind), SQLITE_OK);
}

template <>
  void
  Database::Statement::Bind<int64_t> (const int ind, const int64_t& val)
{
  CHECK_EQ (sqlite3_bind_int64 (**this, ind, val), SQLITE_OK);
}

template <>
  void
  Database::Statement::Bind<uint64_t> (const int ind, const uint64_t& val)
{
  CHECK_LE (val, std::numeric_limits<int64_t>::max ());
  Bind<int64_t> (ind, val);
}

template <>
  void
  Database::Statement::Bind<std::string> (const int ind,
                                          const std::string& val)
{
  CHECK_EQ (sqlite3_bind_text (**this, ind, val.data (), val.size (),
                               SQLITE_TRANSIENT),
            SQLITE_OK);
}

template <>
  void
  Database::Statement::Bind<Json::Value> (const int ind, const Json::Value& val)
{
  Json::StreamWriterBuilder wbuilder;
  wbuilder["commentStyle"] = "None";
  wbuilder["indentation"] = "";
  wbuilder["enableYAMLCompatibility"] = false;
  wbuilder["dropNullPlaceholders"] = false;
  wbuilder["useSpecialFloats"] = false;

  Bind (ind, Json::writeString (wbuilder, val));
}

bool
Database::Statement::IsNull (const int ind) const
{
  return sqlite3_column_type (**this, ind) == SQLITE_NULL;
}

template <>
  int64_t
  Database::Statement::Get<int64_t> (const int ind) const
{
  return sqlite3_column_int64 (**this, ind);
}

template <>
  uint64_t
  Database::Statement::Get<uint64_t> (const int ind) const
{
  const int64_t val = Get<int64_t> (ind);
  CHECK_GE (val, 0);
  return val;
}

template <>
  std::string
  Database::Statement::Get<std::string> (const int ind) const
{
  const int len = sqlite3_column_bytes (**this, ind);
  if (len == 0)
    return std::string ();

  const unsigned char* str = sqlite3_column_text (**this, ind);
  CHECK (str != nullptr);
  return std::string (reinterpret_cast<const char*> (str), len);
}

template <>
  Json::Value
  Database::Statement::Get<Json::Value> (const int ind) const
{
  const auto serialised = Get<std::string> (ind);

  Json::CharReaderBuilder rbuilder;
  rbuilder["allowComments"] = false;
  rbuilder["strictRoot"] = false;
  rbuilder["failIfExtra"] = true;
  rbuilder["rejectDupKeys"] = true;

  Json::Value res;
  std::string parseErrs;
  std::istringstream in(serialised);
  CHECK (Json::parseFromStream (rbuilder, in, &res, &parseErrs))
      << "Invalid JSON in database: " << parseErrs << "\n" << serialised;

  return res;
}

/* ************************************************************************** */

} // namespace xayax
