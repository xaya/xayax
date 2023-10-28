// Copyright (C) 2023 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAX_BLOCKCACHE_HPP
#define XAYAX_BLOCKCACHE_HPP

#include "basechain.hpp"

#include <map>

namespace xayax
{

/**
 * This is an implementation of BaseChain, which uses another BaseChain
 * as "ground truth".  On top of that, it caches blocks seen to some storage
 * (outside of the base-chain client), if they are behind tip by a certain
 * depth (so assumed to be finalised).  When a call to GetBlockRange can
 * be served from stored blocks, it is done instead of resorting back to
 * the blockchain client.
 *
 * This class is implemented to ensure that no extra calls are made to
 * the underlying blockchain client compared to using the underlying
 * BaseChain directly, but that it avoids expensive GetBlockRange calls
 * where it already retrieved those blocks previously.
 */
class BlockCacheChain : public BaseChain
{

public:

  class Storage;

private:

  /** The underlying "ground truth" chain.  */
  BaseChain& base;

  /** The storage to use for block caching.  */
  Storage& store;

  /**
   * Block depth behind tip before blocks are cached.  A block is cached
   * if there are at least minDepth blocks following it in the chain.
   */
  const uint64_t minDepth;

  /**
   * The last tip height seen on the base chain.  We update this whenever
   * GetTipHeight() is called (so as to not produce extra calls).  This
   * happens frequently, as it is part of the sync steps.  The height
   * here is used to judge whether or not a block is already far enough
   * behind to be cached.
   */
  uint64_t lastTipHeight = 0;

public:

  explicit BlockCacheChain (BaseChain& b, Storage& s, const uint64_t md)
    : base(b), store(s), minDepth(md)
  {}

  void SetCallbacks (Callbacks* c) override;

  void Start () override;
  bool EnablePending () override;
  uint64_t GetTipHeight () override;
  std::vector<BlockData> GetBlockRange (uint64_t start,
                                        uint64_t count) override;
  int64_t GetMainchainHeight (const std::string& hash) override;
  std::vector<std::string> GetMempool () override;
  bool VerifyMessage (const std::string& msg,
                      const std::string& signature,
                      std::string& addr) override;
  std::string GetChain () override;
  uint64_t GetVersion () override;

};

/**
 * This is the interface for implementing storage of cached blocks.
 * Each block stored is assumed to be finalised already, and so "should"
 * never change again.
 */
class BlockCacheChain::Storage
{

public:

  Storage () = default;
  virtual ~Storage () = default;

  /**
   * Stores all of the given blocks into the cache.
   */
  virtual void Store (const std::vector<BlockData>& blocks) = 0;

  /**
   * Tries to retrieve blocks from the given range from storage.  If all
   * of the blocks are cached, they should be returned in the right
   * order.  Otherwise, a partial vector of the blocks that are known
   * can be returned.
   */
  virtual std::vector<BlockData> GetRange (uint64_t start, uint64_t count) = 0;

};

/**
 * This is an implementation of the BlockCacheChain::Storage interface,
 * which stores blocks in memory.  This is obviously not very useful (or
 * scalable) in production, but can be used for testing.
 */
class InMemoryBlockStorage : public BlockCacheChain::Storage
{

private:

  /** The blocks stored, keyed by height.  */
  std::map<uint64_t, BlockData> data;

public:

  void Store (const std::vector<BlockData>& blocks) override;
  std::vector<BlockData> GetRange (uint64_t start, uint64_t count) override;

};

} // namespace xayax

#endif // XAYAX_BLOCKCACHE_HPP
