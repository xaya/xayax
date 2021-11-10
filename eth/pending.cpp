// Copyright (C) 2021-2022 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pending.hpp"

#include "contract-constants.hpp"
#include "ethchain.hpp"
#include "hexutils.hpp"

#include <eth-utils/abi.hpp>
#include <eth-utils/address.hpp>

#include <json/json.h>

#include <glog/logging.h>

using ethutils::AbiDecoder;
using ethutils::AbiEncoder;

namespace xayax
{

namespace
{

/**
 * Runs some given constructor bytecode with some ABI-encoded arguments
 * to produce the deployed bytecode for a contract.
 */
std::string
FakeDeploy (EthRpcClient& rpc, const std::string& code, const std::string& args)
{
  /* We need just some dummy address to overlay the code onto and then call,
     but the value doesn't matter at all.  */
  const std::string addr = "0x4242424242424242424242424242424242424242";

  Json::Value tx(Json::objectValue);
  tx["to"] = addr;

  Json::Value state(Json::objectValue);
  /* For running the constructor, the arguments are just appended after the
     initialisation code, and that is then the full code executed.  */
  state["code"] = AbiEncoder::ConcatHex (code, args);
  Json::Value overlay(Json::objectValue);
  overlay[addr] = state;

  return rpc.eth_call (tx, "latest", overlay);
}

} // anonymous namespace

PendingDataExtractor::PendingDataExtractor (EthRpcClient& rpc,
                                            const std::string& acc)
{
  const ethutils::Address accAddr(acc);
  CHECK (accAddr) << "Accounts contract address is invalid";
  accountsContract = accAddr.GetChecksummed ();

  AbiEncoder fwdArgs(1);
  fwdArgs.WriteWord (accountsContract);
  fwdCode = FakeDeploy (rpc, CALL_FORWARDER_CODE, fwdArgs.Finalise ());

  /* Get the address of the WCHI token used by the accounts contract.  */
  Json::Value wchiCall(Json::objectValue);
  wchiCall["to"] = accountsContract;
  wchiCall["data"] = ACCOUNT_WCHI_FCN;
  const Json::Value noOverlay(Json::objectValue);
  AbiDecoder wchiRes(rpc.eth_call (wchiCall, "latest", noOverlay));
  const ethutils::Address wchiAddr(wchiRes.ReadUint (160));
  CHECK (wchiAddr) << "Got invalid WCHI address from RPC";
  LOG (INFO)
      << "The accounts contract is using " << wchiAddr << " as WCHI token";

  AbiEncoder accArgs(1);
  accArgs.WriteWord (wchiAddr.GetChecksummed ());
  accountsOverlayCode = FakeDeploy (rpc, TRACKING_ACCOUNTS_CODE,
                                    accArgs.Finalise ());
}

void
PendingDataExtractor::AddWatchedContract (const std::string& addr)
{
  const ethutils::Address parsed(addr);
  CHECK (parsed) << "Invalid watched contract address: " << addr;
  LOG (INFO) << "Watching transactions to " << parsed << " for potential moves";
  watchedContracts.insert (parsed.GetChecksummed ());
}

std::vector<MoveData>
PendingDataExtractor::GetMoves (EthRpcClient& rpc,
                                const std::string& txid) const
{
  const auto data = rpc.eth_getTransactionByHash (txid);
  CHECK (data.isObject ());

  VLOG (1) << "Received pending transaction: " << txid;
  VLOG (2) << "Transaction details:\n" << data;

  /* If this is not a pending transaction anymore or is a contract
     deployment, just ignore it.  */
  if (!data["blockHash"].isNull () || data["to"].isNull ())
    return {};

  const ethutils::Address from(data["from"].asString ());
  const ethutils::Address to(data["to"].asString ());
  CHECK (from && to) << "Invalid addresses received from RPC";
  const std::string callData = data["input"].asString ();

  /* If this address is not to one of the whitelisted contracts that we
     want to watch for moves, ignore it.  */
  if (watchedContracts.count (to.GetChecksummed ()) == 0)
    {
      VLOG (1) << "Ignoring pending transaction to non-watched target " << to;
      return {};
    }

  AbiEncoder execArgs(2);
  execArgs.WriteWord (to.GetChecksummed ());
  execArgs.WriteBytes (callData);

  /* Construct the call-forwarder transaction overlaid onto the
     from address.  This mimics the actual transaction as closely as
     possible, while returning any move events generated.  */
  Json::Value tx(Json::objectValue);
  tx["to"] = from.GetChecksummed ();
  tx["value"] = data["value"];
  tx["data"] = AbiEncoder::ConcatHex (FORWARDER_EXECUTE_FCN,
                                      execArgs.Finalise ());

  Json::Value overlay(Json::objectValue);

  Json::Value state(Json::objectValue);
  state["code"] = fwdCode;
  overlay[from.GetChecksummed ()] = state;
  state["code"] = accountsOverlayCode;
  overlay[accountsContract] = state;

  try
    {
      const auto res = rpc.eth_call (tx, "latest", overlay);
      return DecodeMoveLogs (txid, res);
    }
  catch (const jsonrpc::JsonRpcException& exc)
    {
      /* This most likely means that the call reverted, but in any case
         let us just ignore the transaction.  */
      LOG (WARNING)
          << "eth_call for pending transaction failed:\n"
          << exc.GetMessage ();
      return {};
    }
}

std::vector<MoveData>
PendingDataExtractor::DecodeMoveLogs (const std::string& txid,
                                      const std::string& hexStr)
{
  AbiDecoder mainDecoder(hexStr);
  size_t len;
  AbiDecoder array = mainDecoder.ReadArray (len);

  std::vector<MoveData> res;
  for (unsigned i = 0; i < len; ++i)
    {
      AbiDecoder dec = array.ReadDynamic ();
      MoveData cur = GetMoveDataFromLogs (dec);
      cur.txid = ConvertUint256 (txid);
      res.push_back (std::move (cur));
    }

  return res;
}

} // namespace xayax
