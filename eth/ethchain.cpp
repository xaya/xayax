// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ethchain.hpp"

#include "abi.hpp"
#include "rpcutils.hpp"

#include "rpc-stubs/ethrpcclient.h"

#include <jsonrpccpp/client.h>

#include <glog/logging.h>

#include <sstream>
#include <map>

namespace xayax
{

/* ************************************************************************** */

namespace
{

using EthRpc = RpcClient<EthRpcClient, jsonrpc::JSONRPC_CLIENT_V2>;

/** Known chain IDs and how they map to libxayagame network strings.  */
const std::map<int64_t, std::string> CHAIN_IDS =
  {
    {137, "polygon"},
    {80001, "mumbai"},
    {1337, "ganache"},
  };

/**
 * Encodes a given integer as hex string.
 */
std::string
EncodeHexInt (const int64_t val)
{
  std::ostringstream out;
  out << "0x" << std::hex << val;
  return out.str ();
}

/**
 * Extracts the base data of a block (without moves) from the
 * eth_getBlockByNumber JSON result.
 */
BlockData
ExtractBaseData (const Json::Value& val)
{
  CHECK (val.isObject ());

  BlockData res;
  res.hash = val["hash"].asString ();
  res.parent = val["parentHash"].asString ();
  res.height = AbiDecoder::ParseInt (val["number"].asString ());
  /* FIXME: Determine proper value for rngseed.  */
  res.rngseed = res.hash;

  res.metadata = Json::Value (Json::objectValue);
  res.metadata["timestamp"]
      = AbiDecoder::ParseInt (val["timestamp"].asString ());

  return res;
}

/**
 * Queries for a range of blocks in a given range of heights.  This method
 * may return false if some error happened, for instance a race condition
 * while doing RPC requests made something inconsistent.
 */
bool
TryBlockRange (EthRpc& rpc, const int64_t startHeight, int64_t endHeight,
               std::vector<BlockData>& res)
{
  CHECK (res.empty ());

  /* As the first step, reduce the endHeight if it is beyond the current tip.
     Afterwards, we expect to get blocks exactly up to endHeight.  */
  const int64_t tipHeight = AbiDecoder::ParseInt (rpc->eth_blockNumber ());
  if (tipHeight < endHeight)
    endHeight = tipHeight;
  if (endHeight < startHeight)
    {
      /* res is empty */
      return true;
    }

  /* Query for the base block data using a batch request over the entire range
     of heights we want.  */
  jsonrpc::BatchCall req;
  std::vector<int> ids;
  std::map<int, int64_t> heightForId;
  for (int64_t h = startHeight; h <= endHeight; ++h)
    {
      Json::Value params(Json::arrayValue);
      params.append (EncodeHexInt (h));
      params.append (false);
      const int id = req.addCall ("eth_getBlockByNumber", params);
      ids.push_back (id);
      heightForId.emplace (id, h);
    }
  jsonrpc::BatchResponse resp = rpc->CallProcedures (req);
  for (const auto id : ids)
    {
      Json::Value idVal(id);
      const int err = resp.getErrorCode (idVal);
      CHECK_EQ (err, 0)
          << "Error " << err << " retrieving block at height "
          << heightForId.at (id) << ":\n"
          << err << resp.getErrorMessage (id);

      const auto blockJson = resp.getResult (id);
      if (blockJson.isNull ())
        {
          /* The block does not exist, maybe due to a race condition.  */
          LOG (WARNING)
              << "Block at height " << heightForId.at (id)
              << " was not found";
          return false;
        }

      BlockData blk = ExtractBaseData (blockJson);
      if (res.empty ())
        CHECK_EQ (blk.height, startHeight);
      else
        {
          CHECK_EQ (blk.height, res.back ().height + 1);
          /* If the block does not fit into the chain we are building up,
             it may be due to a race condition.  */
          if (blk.parent != res.back ().hash)
            {
              LOG (WARNING)
                  << "Mismatch between parent hash of block "
                  << heightForId.at (id) << " (" << blk.parent << ")"
                  << " and previous block hash " << res.back ().hash;
              return false;
            }
        }

      res.push_back (std::move (blk));
    }
  CHECK_EQ (res.back ().height, endHeight);

  /* TODO: Query for move logs and splice them into the block array.  */

  return true;
}

} // anonymous namespace

/* ************************************************************************** */

EthChain::EthChain (const std::string& ep)
  : endpoint(ep)
{}

void
EthChain::Start ()
{
  EthRpc rpc(endpoint);
  LOG (INFO) << "Connected to " << rpc->web3_clientVersion ();

  /* TODO: Set up some polling task for new blocks.  */
}

std::vector<BlockData>
EthChain::GetBlockRange (const uint64_t start, const uint64_t count)
{
  if (count == 0)
    return {};

  EthRpc rpc(endpoint);

  const uint64_t endHeight = start + count - 1;
  CHECK_GE (endHeight, start);

  /* During the RPC calls that we use to get the block range, a race
     condition could occur and result in inconsistent data (as they are
     not atomic).  In this case, we simply try again.  This is unlikely
     to happen in practice (but not impossible).  */
  while (true)
    {
      std::vector<BlockData> res;
      if (TryBlockRange (rpc, start, endHeight, res))
        return res;
    }
}

std::string
EthChain::GetChain ()
{
  EthRpc rpc(endpoint);
  const int64_t chainId = AbiDecoder::ParseInt (rpc->eth_chainId ());
  const auto mit = CHAIN_IDS.find (chainId);
  CHECK (mit != CHAIN_IDS.end ()) << "Unknown Ethereum chain ID: " << chainId;
  return mit->second;
}

uint64_t
EthChain::GetVersion ()
{
  /* What matters here is the interface exposed by Xaya X, not the
     actual version of the underlying Ethereum client.  Thus we return
     our own versioning here.  */
  return 1'00'00'00;
}

/* ************************************************************************** */

} // namespace xayax
