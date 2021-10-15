// Copyright (C) 2021-2022 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAX_ETH_ETHCHAIN_HPP
#define XAYAX_ETH_ETHCHAIN_HPP

#include "pending.hpp"
#include "websocket.hpp"

#include "basechain.hpp"

#include <eth-utils/abi.hpp>
#include <eth-utils/ecdsa.hpp>

#include <memory>

namespace xayax
{

/**
 * Extracts the data for one move from the logs data.  The txid is not
 * part of the logs and not added to MoveData by this method.
 */
MoveData GetMoveDataFromLogs (ethutils::AbiDecoder& dec);

/**
 * BaseChain connector that links to an Ethereum-like network endpoing.
 */
class EthChain : public BaseChain, private WebSocketSubscriber::Callbacks
{

private:

  class BlockMoveExtractor;
  class EthRpc;

  /** EthUtils ECDSA precomputed data.  */
  const ethutils::ECDSA ecdsa;

  /**
   * RPC endpoint for Ethereum.  We store the endpoint as string and
   * establish a new HTTP connection and RPC client for each request, so
   * that they work in a thread-safe way without any fuss.
   */
  const std::string endpoint;

  /** Contract address of the Xaya account registry.  */
  std::string accountsContract;

  /**
   * The extractor for pending move data.  This is set by EnablePending
   * and will remain null if pending tracking is not turned on.
   */
  std::unique_ptr<PendingDataExtractor> pending;

  /**
   * The websocket subscriber we use to get notified about new tips.  It is
   * created if we actually have a ws endpoing.
   */
  std::unique_ptr<WebSocketSubscriber> sub;

  /**
   * Fills in basic options for an eth_getLogs call requesting move events
   * from the Ethereum side.  It does not yet fill in the block filter
   * (heights or blockHash).
   */
  Json::Value GetLogsOptions () const;

  /**
   * Requests move logs for a given list of blocks one-by-one (based on the
   * hashes of the blocks in question) and adds them into the block data.
   * Returns false if something failed, like a block was reorged and the logs
   * are not available anymore.
   */
  bool AddMovesOneByOne (EthRpc& rpc, std::vector<BlockData>& blocks) const;

  /**
   * Requests move logs from Ethereum based on a range of block heights,
   * and adds them into the block data.  This is insecure for blocks that
   * may get reorged away but more efficient than AddMovesOneByOne.  It can
   * be used for very old blocks during syncing.
   */
  void AddMovesFromHeightRange (EthRpc& rpc,
                                std::vector<BlockData>& blocks) const;

  /**
   * Queries for a range of blocks in a given range of heights.  This method
   * may return false if some error happened, for instance a race condition
   * while doing RPC requests made something inconsistent.
   */
  bool TryBlockRange (EthRpc& rpc, const int64_t startHeight, int64_t endHeight,
                      std::vector<BlockData>& res) const;

  void NewTip (const std::string& tip) override;
  void NewPendingTx (const std::string& txid) override;

public:

  /**
   * Constructs a new instance.  It requires both an HTTP and a WebSocket
   * endpoint.  The HTTP endpoint is used for normal requests, and the
   * WebSocket for subscriptions.
   */
  explicit EthChain (const std::string& httpEndpoint,
                     const std::string& wsEndpoint,
                     const std::string& acc);

  void Start () override;
  bool EnablePending () override;

  uint64_t GetTipHeight () override;
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

#endif // XAYAX_ETH_ETHCHAIN_HPP
