// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAX_CORE_CORECHAIN_HPP
#define XAYAX_CORE_CORECHAIN_HPP

#include "basechain.hpp"

namespace xayax
{

/**
 * BaseChain connector that links back to a Xaya Core instance.  This is mainly
 * useful for testing, but could also help as part of a unified Xaya X framework
 * in the future, where all GSPs run with Xaya X.
 */
class CoreChain : public BaseChain
{

private:

  /**
   * RPC endpoint for Xaya Core.  We store the endpoint as string and
   * establish a new HTTP connection and RPC client for each request, so
   * that they work in a thread-safe way without any fuss.
   */
  const std::string endpoint;

  /* FIXME: Include ZMQ listener to push tip changes.  */

  friend class CoreRpc;

public:

  explicit CoreChain (const std::string& ep)
    : endpoint(ep)
  {}

  std::vector<BlockData> GetBlockRange (uint64_t start,
                                        uint64_t count) override;
  std::string GetChain () override;
  uint64_t GetVersion () override;

};

} // namespace xayax

#endif // XAYAX_CORECHAIN_HPP
