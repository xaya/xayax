// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAX_ETH_PENDING_HPP
#define XAYAX_ETH_PENDING_HPP

#include "blockdata.hpp"

#include "rpc-stubs/ethrpcclient.h"

#include <string>
#include <vector>

namespace xayax
{

/**
 * Extractor for move data from pending transactions.  It uses eth_call
 * with a state overlay to simulate the transaction, and extract the
 * generated move events by instrumenting the accounts contract.
 */
class PendingDataExtractor
{

private:

  /** The address of the accounts contract.  */
  std::string accountsContract;

  /**
   * The "deployed" bytecode of the CallForwarder contract.  This is
   * not actually deployed, but is the result of executing the constructor
   * code on the EVM to set the immutable variables.
   */
  std::string fwdCode;

  /** The "deployed" bytecode of the TrackingAccounts contract.  */
  std::string accountsOverlayCode;

  /**
   * Decodes an ABI-encoded result of move logs from the CallForwarder
   * into the MoveData structure.
   */
  static std::vector<MoveData> DecodeMoveLogs (const std::string& txid,
                                               const std::string& hexStr);

  friend class PendingDataExtractorTests;

public:

  /**
   * Constructs the instance, which already computes the deployed bytecodes
   * for our overlay contracts (using the RPC interface with eth_call).
   */
  explicit PendingDataExtractor (EthRpcClient& rpc, const std::string& acc);

  /**
   * Extracts the moves (if any) from a new pending tx received.
   */
  std::vector<MoveData> GetMoves (EthRpcClient& rpc,
                                  const std::string& txid) const;

};

} // namespace xayax

#endif // XAYAX_ETH_PENDING_HPP
