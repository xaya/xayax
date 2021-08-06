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
#include <map>
#include <sstream>

namespace xayax
{

namespace
{

/**
 * Converts a hex string to a binary string.
 */
std::string
Unhexlify (const std::string& hex)
{
  CHECK_EQ (hex.size () % 2, 0);

  std::string res;
  for (size_t i = 0; i < hex.size (); i += 2)
    {
      std::istringstream in(hex.substr (i, 2));
      int val;
      in >> std::hex >> val;
      res.push_back (static_cast<char> (val));
    }

  return res;
}

/**
 * Converts a JSON transaction to a MoveData instance.  Returns false
 * if there is no actual move in the transaction.
 */
bool
GetMoveFromTx (const Json::Value& data, MoveData& mv)
{
  std::map<std::string, double> outAmounts;
  std::map<std::string, double> burns;
  Json::Value nameOp;
  for (const auto& out : data["vout"])
    {
      CHECK (out.isObject ());
      const auto& scriptPubKey = out["scriptPubKey"];
      CHECK (scriptPubKey.isObject ());

      if (scriptPubKey.isMember ("nameOp"))
        {
          CHECK (nameOp.isNull ()) << "Two name operations in tx";
          nameOp = scriptPubKey["nameOp"];
          continue;
        }

      if (scriptPubKey.isMember ("address"))
        {
          const auto& addrVal = scriptPubKey["address"];
          CHECK (addrVal.isString ());
          const std::string addr = addrVal.asString ();
          if (outAmounts.count (addr) == 0)
            outAmounts.emplace (addr, 0.0);
          outAmounts[addr] += out["value"].asDouble ();
        }

      if (scriptPubKey.isMember ("burn"))
        {
          const auto& burnVal = scriptPubKey["burn"];
          CHECK (burnVal.isString ());
          std::string burn = Unhexlify (burnVal.asString ());
          if (burn.substr (0, 2) == "g/")
            {
              burn = burn.substr (2);
              if (burns.count (burn) == 0)
                burns.emplace (burn, 0.0);
              burns[burn] += out["value"].asDouble ();
            }
        }
    }

  if (nameOp.isNull ())
    return false;
  CHECK (nameOp.isObject ());

  CHECK_EQ (nameOp["name_encoding"].asString (), "utf8")
      << "Xaya Core's name_encoding should be UTF-8";
  CHECK_EQ (nameOp["value_encoding"].asString (), "utf8")
      << "Xaya Core's value_encoding should be UTF-8";

  /* This would happen if the name or value is invalid for the encoding.
     Xaya's consensus rules enforce that either of them are valid UTF-8,
     though, so this should never happen.  */
  CHECK (nameOp.isMember ("name") && nameOp.isMember ("value"))
      << "Name op does not contain name or value:\n"
      << nameOp;

  const std::string fullName = nameOp["name"].asString ();
  const size_t nsPos = fullName.find ('/');
  CHECK_NE (nsPos, std::string::npos)
      << "Name does not contain a namespace: " << fullName;

  mv.txid = data["txid"].asString ();
  mv.ns = fullName.substr (0, nsPos);
  mv.name = fullName.substr (nsPos + 1);
  mv.mv = nameOp["value"].asString ();

  mv.metadata = Json::Value (Json::objectValue);
  mv.metadata["btxid"] = data["btxid"].asString ();

  Json::Value outJson(Json::objectValue);
  for (const auto& entry : outAmounts)
    outJson[entry.first] = entry.second;
  mv.metadata["out"] = outJson;

  mv.burns.clear ();
  for (const auto& entry : burns)
    mv.burns.emplace (entry.first, Json::Value (entry.second));

  Json::Value inputs(Json::arrayValue);
  for (const auto& in : data["vin"])
    {
      Json::Value cur(Json::objectValue);
      cur["txid"] = in["txid"].asString ();
      cur["vout"] = in["vout"].asInt ();
      inputs.append (cur);
    }
  mv.metadata["inputs"] = inputs;

  return true;
}

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

  for (const auto& tx : data["tx"])
    {
      MoveData mv;
      if (GetMoveFromTx (tx, mv))
        res.moves.push_back (std::move (mv));
    }

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
