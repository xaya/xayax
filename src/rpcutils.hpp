// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAX_RPCUTILS_HPP
#define XAYAX_RPCUTILS_HPP

#include <jsonrpccpp/client.h>
#include <jsonrpccpp/client/connectors/httpclient.h>

#include <string>

namespace xayax
{

/**
 * Converts a binary string to hex.
 */
std::string Hexlify (const std::string& bin);

/**
 * Converts a hex string into a binary string.  Returns false if the input
 * string is not valid hex.
 */
bool Unhexlify (const std::string& hex, std::string& bin);

/**
 * Simple wrapper around a JSON-RPC connection to some HTTP endpoint.
 * We use a fresh instance of this every time we need one, just for simplicity
 * and to ensure thread safety.
 */
template <typename T, jsonrpc::clientVersion_t V>
  class RpcClient
{

private:

  /** HTTP client instance.  */
  jsonrpc::HttpClient http;

  /** Actual JSON-RPC client.  */
  T rpc;

public:

  explicit RpcClient (const std::string& ep)
    : http(ep), rpc(http, V)
  {}

  T*
  operator-> ()
  {
    return &rpc;
  }

};

} // namespace xayax

#endif // XAYAX_RPCUTILS_HPP
