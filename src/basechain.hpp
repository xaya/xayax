// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAX_BASECHAIN_HPP
#define XAYAX_BASECHAIN_HPP

#include "blockdata.hpp"

#include <cstdint>
#include <vector>

namespace xayax
{

/**
 * Interface that the "base chain" connector needs to implement to provide
 * Xaya X with the raw data on the connected blockchain.
 */
class BaseChain
{

public:

  class Callbacks;

private:

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
