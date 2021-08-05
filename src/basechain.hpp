// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAX_BASECHAIN_HPP
#define XAYAX_BASECHAIN_HPP

#include "blockdata.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace xayax
{

/**
 * Interface that the "base chain" connector needs to implement to provide
 * Xaya X with the raw data on the connected blockchain.  The implemented
 * methods by subclasses may be called in parallel and should be thread-safe.
 */
class BaseChain
{

public:

  class Callbacks;

private:

  /** Lock for this instance (mainly the callback pointer).  */
  std::mutex mut;

  /** The currently active callbacks (or none).  */
  Callbacks* cb = nullptr;

protected:

  /**
   * When the best chain tip changes on the underlying base chain, this
   * method can be used to notify about this from implementations.
   */
  void TipChanged ();

public:

  BaseChain () = default;
  virtual ~BaseChain () = default;

  /**
   * Sets a callbacks instance that will receive notifications about
   * new blocks and transactions.
   */
  void SetCallbacks (Callbacks* c);

  /**
   * Called after the instance is created and before it is getting used.
   * This can be overridden to e.g. start a listening or polling thread.
   * Any cleanup / stopping should be done in the destructor.
   *
   * Does nothing in the default implementation.
   */
  virtual void
  Start ()
  {}

  /**
   * Retrieves a slice of blocks with all associated data (block metadata
   * and contained moves) on the main chain from height start (inclusive)
   * onward.  If there are no or fewer than count blocks on the main chain
   * after height start, none or fewer should be returned in the result.
   */
  virtual std::vector<BlockData> GetBlockRange (uint64_t start,
                                                uint64_t count) = 0;

  /**
   * Returns a string identifying the underlying chain / network this
   * corresponds to.  The string should be one of the supported strings
   * by libxayagame.
   *
   * This method is assumed to always return the same value during the
   * lifetime of the process.
   */
  virtual std::string GetChain () = 0;

  /**
   * Returns an integer indicating the version of the basechain daemon
   * (and/or the basechain implementation).  This can be used by GSPs to
   * ensure they are connected to a daemon of a version that is new enough,
   * e.g. if the format of move or block data was extended with more fields.
   *
   * This method is assumed to always return the same value during the
   * lifetime of the process.
   */
  virtual uint64_t GetVersion () = 0;

};

/**
 * Interface for user-provided callbacks that can receive push notifications
 * about new blocks and other updates from the base chain.
 */
class BaseChain::Callbacks
{

public:

  Callbacks () = default;
  virtual ~Callbacks () = default;

  /**
   * Invoked when the active tip of the basechain is changed.
   */
  virtual void TipChanged () = 0;

};

} // namespace xayax

#endif // XAYAX_BASECHAIN_HPP
