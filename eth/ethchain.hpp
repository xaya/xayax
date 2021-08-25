// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAX_ETH_ETHCHAIN_HPP
#define XAYAX_ETH_ETHCHAIN_HPP

#include "websocket.hpp"

#include "basechain.hpp"

#include <memory>

namespace xayax
{

/**
 * BaseChain connector that links to an Ethereum-like network endpoing.
 */
class EthChain : public BaseChain, private WebSocketSubscriber::Callbacks
{

private:

  /**
   * RPC endpoint for Ethereum.  We store the endpoint as string and
   * establish a new HTTP connection and RPC client for each request, so
   * that they work in a thread-safe way without any fuss.
   */
  const std::string endpoint;

  /**
   * The websocket subscriber we use to get notified about new tips.  It is
   * created if we actually have a ws endpoing.
   */
  std::unique_ptr<WebSocketSubscriber> sub;

  void NewTip () override;

public:

  /**
   * Constructs a new instance.  It requires both an HTTP and a WebSocket
   * endpoint.  The HTTP endpoint is used for normal requests, and the
   * WebSocket for subscriptions.
   */
  explicit EthChain (const std::string& httpEndpoint,
                     const std::string& wsEndpoint);

  void Start () override;

  std::vector<BlockData> GetBlockRange (uint64_t start,
                                        uint64_t count) override;
  std::string GetChain () override;
  uint64_t GetVersion () override;

};

} // namespace xayax

#endif // XAYAX_ETH_ETHCHAIN_HPP
