// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ethchain.hpp"

#include "abi.hpp"
#include "rpcutils.hpp"

#include "rpc-stubs/ethrpcclient.h"

#include <jsonrpccpp/client.h>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <cmath>
#include <map>
#include <sstream>
#include <utility>

namespace xayax
{

DEFINE_int32 (ethchain_fast_logs_depth, 1'024,
              "use faster log retrieval for blocks buried this far in the"
              " blockchain (they should never get reorged again)");

/* ************************************************************************** */

class EthChain::EthRpc
    : public RpcClient<EthRpcClient, jsonrpc::JSONRPC_CLIENT_V2>
{
public:
  using RpcClient::RpcClient;
};

namespace
{

/** Known chain IDs and how they map to libxayagame network strings.  */
const std::map<int64_t, std::string> CHAIN_IDS =
  {
    {137, "polygon"},
    {80'001, "mumbai"},
    {1'337, "ganache"},
  };

/** Event signature of moves.  */
constexpr const char* MOVE_EVENT
    = "Move(string,string,string,uint256,uint256,address,uint256,address)";

/**
 * Decimals (precision) of the CHI token.  We could in theory extract
 * that from the contract, but in practice it will be fixed to 8 places
 * anyway.  Also having 8 here ensures that the value can be represented
 * with a standard double correctly.
 */
constexpr int DECIMALS = 8;

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
 * Converts a uint256 hash with 0x prefix as from the Ethereum RPC
 * interface to one without prefix as libxayagame expects them.
 */
std::string
ConvertUint256 (const std::string& withPrefix)
{
  CHECK_EQ (withPrefix.substr (0, 2), "0x");
  return withPrefix.substr (2);
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
  res.hash = ConvertUint256 (val["hash"].asString ());
  res.parent = ConvertUint256 (val["parentHash"].asString ());
  res.height = AbiDecoder::ParseInt (val["number"].asString ());
  /* FIXME: Determine proper value for rngseed.  */
  res.rngseed = res.hash;

  res.metadata = Json::Value (Json::objectValue);
  res.metadata["timestamp"]
      = AbiDecoder::ParseInt (val["timestamp"].asString ());

  return res;
}

/**
 * Extracts move data from a log event JSON.
 */
MoveData
ExtractMove (const Json::Value& val)
{
  MoveData res;
  res.txid = ConvertUint256 (val["transactionHash"].asString ());
  res.metadata = Json::Value (Json::objectValue);

  AbiDecoder dec(val["data"].asString ());
  res.ns = dec.ReadString ();
  res.name = dec.ReadString ();
  res.mv = dec.ReadString ();

  /* Ignore nonce and mover.  */
  dec.ReadUint (256);
  dec.ReadUint (160);

  const int64_t amount = AbiDecoder::ParseInt (dec.ReadUint (256));
  const std::string receiver = dec.ReadUint (160);

  Json::Value out(Json::objectValue);
  CHECK_GE (amount, 0);
  if (amount > 0)
    out[receiver] = static_cast<double> (amount) / std::pow (10.0, DECIMALS);
  res.metadata["out"] = out;

  return res;
}

} // anonymous namespace

/* ************************************************************************** */

EthChain::EthChain (const std::string& httpEndpoint,
                    const std::string& wsEndpoint,
                    const std::string& acc)
  : endpoint(httpEndpoint)
{
  if (wsEndpoint.empty ())
    LOG (WARNING) << "Not using WebSocket subscriptions";
  else
    sub = std::make_unique<WebSocketSubscriber> (wsEndpoint);

  /* Convert the accounts contract to all lower-case.  This ensures that
     it will match up with the data returned in logs in our cross-checks,
     while it allows users to pass in the checksum'ed version.  */
  accountsContract = AbiEncoder::ToLower (acc);
}

void
EthChain::NewTip (const std::string& tip)
{
  /* We use block hashes without 0x prefix internally, so need to strip
     off the prefix here as well (even though in the end it is just a string
     in this place and not a true uint256 value).  */
  TipChanged (ConvertUint256 (tip));
}

void
EthChain::Start ()
{
  EthRpc rpc(endpoint);
  LOG (INFO) << "Connected to " << rpc->web3_clientVersion ();

  /* Precompute the topic for moves using the RPC server's Keccak
     method.  We only need to do this once.  */
  moveTopic = GetEventTopic (*rpc, MOVE_EVENT);

  if (sub != nullptr)
    sub->Start (*this);
}

Json::Value
EthChain::GetLogsOptions () const
{
  Json::Value res(Json::objectValue);
  res["address"] = accountsContract;

  Json::Value topics(Json::arrayValue);
  topics.append (moveTopic);
  res["topics"] = topics;

  return res;
}

bool
EthChain::TryBlockRange (EthRpc& rpc, const int64_t startHeight,
                         int64_t endHeight, std::vector<BlockData>& res) const
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

  /* Add in the move data from logs.  There are two methods to do this:
     The safe way is to query for move logs for each block by hash individually,
     and the fast is to query for all logs in a given height range.  The latter
     is susceptible to (potentially undetectable) race conditions in case
     of a reorg, so we only want to use it if the end height is already
     far behind the current tip, i.e. for the bulk of syncing.  */
  if (endHeight + FLAGS_ethchain_fast_logs_depth < tipHeight)
    {
      AddMovesFromHeightRange (rpc, res);
      return true;
    }

  return AddMovesOneByOne (rpc, res);
}

/**
 * Helper class for extracting move data from an eth_getLogs response
 * and adding them to a given block.  We require moves to be ordered,
 * and this class also keeps track of the logIndex/transactionIndex for
 * each event and asserts that they are always increasing.
 */
class EthChain::BlockMoveExtractor
{

private:

  /** The parent EthChain instance.  */
  const EthChain& parent;

  /** The block being extended with moves.  */
  BlockData& blk;

  /**
   * The transactionIndex/logIndex pair we extracted last, which should
   * always increase as a tuple.  Note that in theory logIndex alone should
   * be per-block, but there is a bug in Ganache and some discussions of
   * changing this to be per-transaction.  By comparing the full pair, we
   * ensure that it works correctly in any of these cases.
   */
  std::pair<int64_t, int64_t> lastIndices;

public:

  explicit BlockMoveExtractor (const EthChain& p, BlockData& b)
    : parent(p), blk(b), lastIndices(-1, -1)
  {}

  /**
   * Extracts data from a given eth_getLogs result entry, adding it
   * as a new move to the block we are working on.
   */
  void
  ProcessLogEntry (const Json::Value& log)
  {
    CHECK (log.isObject ());

    CHECK_EQ (log["address"].asString (), parent.accountsContract);
    CHECK_EQ (log["topics"][0].asString (), parent.moveTopic);
    CHECK_EQ (ConvertUint256 (log["blockHash"].asString ()), blk.hash);

    const int64_t txIndex
        = AbiDecoder::ParseInt (log["transactionIndex"].asString ());
    const int64_t logIndex
        = AbiDecoder::ParseInt (log["logIndex"].asString ());
    const auto curIndices = std::make_pair (txIndex, logIndex);
    CHECK (curIndices > lastIndices) << "Logs misordered in result";

    lastIndices = curIndices;
    blk.moves.push_back (ExtractMove (log));
  }

};

bool
EthChain::AddMovesOneByOne (EthRpc& rpc, std::vector<BlockData>& blocks) const
{
  jsonrpc::BatchCall req;
  std::vector<int> ids;
  std::map<int, BlockData*> blkForId;
  for (auto& blk : blocks)
    {
      auto options = GetLogsOptions ();
      options["blockHash"] = "0x" + blk.hash;
      Json::Value params(Json::arrayValue);
      params.append (options);

      const int id = req.addCall ("eth_getLogs", params);
      ids.push_back (id);
      blkForId.emplace (id, &blk);
    }

  jsonrpc::BatchResponse resp = rpc->CallProcedures (req);
  for (const auto id : ids)
    {
      BlockData& blk = *(blkForId.at (id));
      BlockMoveExtractor extractor(*this, blk);

      Json::Value idVal(id);
      const int err = resp.getErrorCode (idVal);
      if (err != 0)
        {
          LOG (WARNING)
              << "Error " << err << " retrieving logs for block "
              << blk.hash << ":\n"
              << err << resp.getErrorMessage (id);
          return false;
        }

      const auto logs = resp.getResult (id);
      CHECK (logs.isArray ());
      for (const auto& l : logs)
        extractor.ProcessLogEntry (l);
    }

  return true;
}

void
EthChain::AddMovesFromHeightRange (EthRpc& rpc,
                                   std::vector<BlockData>& blocks) const
{
  std::map<std::string, std::unique_ptr<BlockMoveExtractor>> extForHash;
  int64_t lastHeight = -1;
  for (auto& blk : blocks)
    {
      if (lastHeight != -1)
        CHECK_EQ (blk.height, lastHeight + 1);
      lastHeight = blk.height;

      auto extractor = std::make_unique<BlockMoveExtractor> (*this, blk);
      extForHash.emplace (blk.hash, std::move (extractor));
    }

  const auto startHeight = blocks.front ().height;
  const auto endHeight = blocks.back ().height;

  auto options = GetLogsOptions ();
  options["fromBlock"] = EncodeHexInt (startHeight);
  options["toBlock"] = EncodeHexInt (endHeight);
  const auto logs = rpc->eth_getLogs (options);

  CHECK (logs.isArray ());
  for (const auto& l : logs)
    {
      const std::string hash = ConvertUint256 (l["blockHash"].asString ());
      const auto mit = extForHash.find (hash);
      CHECK (mit != extForHash.end ())
          << "Events returned for block " << hash
          << ", which is not part of the original block range";
      mit->second->ProcessLogEntry (l);
    }
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

std::vector<std::string>
EthChain::GetMempool ()
{
  /* FIXME: Implement this for real.  */
  return {};
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
