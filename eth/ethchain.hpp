// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAX_ETH_ETHCHAIN_HPP
#define XAYAX_ETH_ETHCHAIN_HPP

#include "basechain.hpp"

namespace xayax
{

/**
 * BaseChain connector that links to an Ethereum-like network endpoing.
 */
class EthChain : public BaseChain
{

private:

  /**
   * RPC endpoint for Ethereum.  We store the endpoint as string and
   * establish a new HTTP connection and RPC client for each request, so
   * that they work in a thread-safe way without any fuss.
   */
  const std::string endpoint;

public:

  explicit EthChain (const std::string& ep);

  void Start () override;

  std::vector<BlockData> GetBlockRange (uint64_t start,
                                        uint64_t count) override;
  std::string GetChain () override;
  uint64_t GetVersion () override;

};

} // namespace xayax

#endif // XAYAX_ETH_ETHCHAIN_HPP
