// Copyright (C) 2021-2023 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAX_RPCUTILS_HPP
#define XAYAX_RPCUTILS_HPP

#include <jsonrpccpp/client.h>
#include <jsonrpccpp/client/connectors/httpclient.h>

#include <chrono>
#include <map>
#include <string>

namespace xayax
{

/** A list of headers that can be added to the requests.  */
using RpcHeaders = std::map<std::string, std::string>;

/**
 * Parses a string into a list of headers.  The format is:
 *  header1=value1;header2=value2;...
 */
RpcHeaders ParseRpcHeaders (const std::string& str);

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

  /**
   * Sets the timeout duration for the RPC call.
   */
  template <typename Rep, typename Period>
    void
    SetTimeout (const std::chrono::duration<Rep, Period>& val)
  {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (val);
    http.SetTimeout (ms.count ());
  }

  /**
   * Adds a list of headers to be sent.
   */
  void
  AddHeaders (const RpcHeaders& headers)
  {
    for (const auto& h : headers)
      http.AddHeader (h.first, h.second);
  }

  T&
  operator* ()
  {
    return rpc;
  }

  T*
  operator-> ()
  {
    return &rpc;
  }

};

} // namespace xayax

#endif // XAYAX_RPCUTILS_HPP
