// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAX_DATABASE_HPP
#define XAYAX_DATABASE_HPP

#include <sqlite3.h>

#include <map>
#include <memory>
#include <string>

namespace xayax
{

/**
 * Basic wrapper around an SQLite database, which implements extra functionality
 * like a cache of prepared statements.  The database is opened in
 * multi-thread mode, which means that calls are not automatically thread-safe
 * and external synchronisation must be used with this instance.
 */
class Database
{

private:

  class CachedStatement;

  /** The underlying SQLite handle.  */
  sqlite3* db;

  /** Cache of prepared statements.  */
  mutable std::map<std::string, std::unique_ptr<CachedStatement>> statements;

public:

  class Statement;

  /**
   * Opens the database at the given filename into this instance.
   */
  explicit Database (const std::string& file);

  /**
   * Closes the database and frees all resources.
   */
  ~Database ();

  /**
   * Directly runs a particular SQL statement on the database, without
   * going through a prepared statement.  This can be useful for things like
   * setting up the schema.
   */
  void Execute (const std::string& sql);

  /**
   * Prepares an SQL statement given as string and stores it in the cache,
   * or retrieves the existing statement from the cache.  The prepared statement
   * is also reset, so that it can be reused right away.  The cache takes
   * care of transparently giving out and releasing statements.
   *
   * Note that each particular statement (string) can only be in use
   * at most once at any given moment; else this method aborts.
   */
  Statement Prepare (const std::string& sql);

  /**
   * Prepares an SQL statement given as string like Prepare.  This method
   * is meant for statements that are read-only, i.e. SELECT.
   */
  Statement PrepareRo (const std::string& sql) const;

  /**
   * Returns the number of rows modified in the most recent update statement.
   */
  unsigned RowsModified () const;

};

/**
 * An entry into the cache of prepared statements.  This instance handles
 * RAII cleanup of the underlying sqlite3_stmt and also has a flag to ensure
 * any given statement is not being used multiple times in parallel.
 */
struct Database::CachedStatement
{

  /** The underlying SQLite statement.  */
  sqlite3_stmt* const stmt;

  /** Whether or not the statement is currently in use.  */
  bool used;

  /**
   * Constructs an instance based on an existing SQLite statement.
   */
  explicit CachedStatement (sqlite3_stmt* s)
    : stmt(s), used(false)
  {}

  CachedStatement () = delete;
  CachedStatement (const CachedStatement&) = delete;
  void operator= (const CachedStatement&) = delete;

  /**
   * Cleans up the SQLite statement and ensures the statement is not in use.
   */
  ~CachedStatement ();

  /**
   * Marks this instance as being used.  Aborts if it is already in use.
   */
  void Acquire ();

  /**
   * Marks this instance as not being used anymore, and also resets
   * all execution and parameters (so it can be freely reused in the
   * future).
   */
  void Release ();

};

/**
 * Abstraction around an SQLite prepared statement.  It provides some
 * basic utility methods that make working with it easier, and also enables
 * RAII semantics for acquiring / releasing prepared statements from the
 * built-in statement cache.
 */
class Database::Statement
{

private:

  /**
   * The underlying cached statement.  The statement is marked as unused
   * when this instance goes out of scope.
   */
  CachedStatement* entry = nullptr;

  /**
   * Constructs a statement instance based on the cache entry.  This marks
   * the cache entry as being in use.
   */
  explicit Statement (CachedStatement& s);

  /**
   * Releases the statement referred to and sets it to null.
   */
  void Clear ();

  /**
   * Returns the underlying sqlite3_stmt, checking that it exists.
   */
  sqlite3_stmt* operator* () const;

  friend class Database;

public:

  Statement () = default;
  Statement (Statement&&);
  Statement& operator= (Statement&&);

  ~Statement ();

  Statement (const Statement&) = delete;
  void operator= (const Statement&) = delete;

  /**
   * Executes the statement without expecting any results (i.e. for anything
   * that is not SELECT).
   */
  void Execute ();

  /**
   * Steps the statement.  This asserts that no error is returned.  It returns
   * true if there are more rows (i.e. sqlite3_step returns SQLITE_ROW) and
   * false if not (SQLITE_DONE).
   */
  bool Step ();

  /**
   * Binds a numbered parameter to NULL.
   */
  void BindNull (int ind);

  /**
   * Binds a typed value to a numbered parameter.
   */
  template <typename T>
    void Bind (int ind, const T& val);

  /**
   * Binds a BLOB value (not TEXT as is otherwise done for std::string).
   */
  void BindBlob (int ind, const std::string& val);

  /**
   * Checks if the numbered column is NULL in the current row.
   */
  bool IsNull (int ind) const;

  /**
   * Extracts a typed value from the column with the given index in the
   * current row.
   */
  template <typename T>
    T Get (int ind) const;

  /**
   * Extracts a BLOB value as byte string.
   */
  std::string GetBlob (int ind) const;

};

} // namespace xayax

#endif // XAYAX_DATABASE_HPP
