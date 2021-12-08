// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAX_CORE_CORECHAIN_HPP
#define XAYAX_CORE_CORECHAIN_HPP

#include "basechain.hpp"

#include <memory>

namespace zmq
{
  class context_t;
} // namespace zmq

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

  class ZmqListener;

  /**
   * RPC endpoint for Xaya Core.  We store the endpoint as string and
   * establish a new HTTP connection and RPC client for each request, so
   * that they work in a thread-safe way without any fuss.
   */
  const std::string endpoint;

  /** ZMQ context used to listen to Xaya Core.  */
  std::unique_ptr<zmq::context_t> zmqCtx;

  /**
   * Active ZMQ listeners per address they are connected to.  We use this
   * so we can add a subscription later on to an already connected listener
   * if the desired notification is using the same address (and thus reuse
   * a single existing socket to ensure in-order handling of messages).
   */
  std::map<std::string, std::unique_ptr<ZmqListener>> listeners;

  /**
   * Gets and returns the ZmqListener for a given address.  If there is none
   * yet, we construct a new one.
   */
  ZmqListener& GetListenerForAddress (const std::string& addr);

public:

  explicit CoreChain (const std::string& ep);
  ~CoreChain ();

  void Start () override;
  bool EnablePending () override;

  std::vector<BlockData> GetBlockRange (uint64_t start,
                                        uint64_t count) override;
  int64_t GetMainchainHeight (const std::string& hash) override;
  std::vector<std::string> GetMempool () override;
  bool VerifyMessage (const std::string& msg, const std::string& signature,
                      std::string& addr) override;
  std::string GetChain () override;
  uint64_t GetVersion () override;

};

} // namespace xayax

#endif // XAYAX_CORECHAIN_HPP
