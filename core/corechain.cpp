// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "corechain.hpp"

#include "rpc-stubs/corerpcclient.h"

#include <jsonrpccpp/client.h>
#include <jsonrpccpp/client/connectors/httpclient.h>
#include <jsonrpccpp/common/exception.h>

#include <glog/logging.h>

#include <algorithm>

namespace xayax
{

namespace
{

/**
 * Converts the getblock JSON data to a BlockData instance.
 */
BlockData
ConstructBlockData (const Json::Value& data)
{
  CHECK (data.isObject ());

  BlockData res;

  res.hash = data["hash"].asString ();
  res.height = data["height"].asUInt64 ();
  if (data.isMember ("previousblockhash"))
    res.parent = data["previousblockhash"].asString ();
  res.rngseed = data["rngseed"].asString ();

  res.metadata = Json::Value (Json::objectValue);
  res.metadata["timestamp"] = data["time"].asInt64 ();
  res.metadata["mediantime"] = data["mediantime"].asInt64 ();

  /* FIXME: Handle move data */

  return res;
}

} // anonymous namespace

class CoreRpc
{

private:

  /** HTTP server instance.  */
  jsonrpc::HttpClient http;

  /** RPC client.  */
  CoreRpcClient rpc;

public:

  explicit CoreRpc (const CoreChain& chain)
    : http(chain.endpoint), rpc(http, jsonrpc::JSONRPC_CLIENT_V1)
  {}

  CoreRpcClient*
  operator-> ()
  {
    return &rpc;
  }

};

std::vector<BlockData>
CoreChain::GetBlockRange (const uint64_t start, const uint64_t count)
{
  if (count == 0)
    return {};

  CoreRpc rpc(*this);
  const uint64_t endHeight = start + count - 1;
  CHECK_GE (endHeight, start);

  /* We first determine the hash of the target block; which is either
     the best tip or a block at the target height.  Once this is done,
     we can go back from there and retrieve all block data leading up to
     it until the start height, in an atomic fashion.  */
  std::string endHash;
  while (true)
    {
      const auto blockchain = rpc->getblockchaininfo ();
      if (blockchain["blocks"].asUInt64 () < start)
        return {};
      if (blockchain["blocks"].asUInt64 () <= endHeight)
        {
          endHash = blockchain["bestblockhash"].asString ();
          break;
        }

      try
        {
          endHash = rpc->getblockhash (endHeight);
          break;
        }
      catch (const jsonrpc::JsonRpcException& exc)
        {
          if (exc.GetCode () != -8)
            throw;
          /* There is no block at this height.  This can happen due to
             a race condition when blocks were just detached.  Just try
             again in this case.  */
        }
    }

  std::vector<BlockData> res;
  do
    {
      const auto data = rpc->getblock (endHash, 2);
      auto cur = ConstructBlockData (data);

      CHECK_GE (cur.height, start);
      endHash = cur.parent;

      res.push_back (std::move (cur));
    }
  while (res.back ().height > start);

  std::reverse (res.begin (), res.end ());
  return res;
}

std::string
CoreChain::GetChain ()
{
  CoreRpc rpc(*this);
  const auto info = rpc->getblockchaininfo ();
  return info["chain"].asString ();
}

uint64_t
CoreChain::GetVersion ()
{
  CoreRpc rpc(*this);
  const auto info = rpc->getnetworkinfo ();
  return info["version"].asUInt64 ();
}

} // namespace xayax
