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

#include <algorithm>

using ethutils::AbiDecoder;
using ethutils::AbiEncoder;

namespace xayax
{

/* ************************************************************************** */

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

/* ************************************************************************** */

void
PendingMempool::Add (const std::string& txid)
{
  std::lock_guard<std::mutex> lock(mut);
  pool.insert (txid);
}

namespace
{

/**
 * Compares two transaction JSON values according to the ordering in which
 * we want to return the mempool content.  This mainly orders by nonce
 * within a sender address, and orders also by sender address to ensure
 * we get a well-defined ordering overall (even though the order in which
 * transactions from multiple senders are confirmed is not defined).
 */
bool
IsEarlierForMempool (const Json::Value& a, const Json::Value& b)
{
  CHECK (a.isObject ());
  CHECK (b.isObject ());
  const ethutils::Address fromA(a["from"].asString ());
  const ethutils::Address fromB(b["from"].asString ());
  CHECK (fromA && fromB) << "Invalid addresses returned in RPC";
  if (fromA != fromB)
    return fromA.GetLowerCase () < fromB.GetLowerCase ();

  const int64_t nonceA = AbiDecoder::ParseInt (a["nonce"].asString ());
  const int64_t nonceB = AbiDecoder::ParseInt (b["nonce"].asString ());
  return nonceA < nonceB;
}

} // anonymous namespace

std::vector<std::string>
PendingMempool::GetContent (EthRpcClient& rpc)
{
  /* We check the underlying node's status of each transaction that is in our
     local pool (with a single batch call).  */
  jsonrpc::BatchCall req;
  std::vector<std::pair<int, std::string>> idsWithTxid;
  {
    std::lock_guard<std::mutex> lock(mut);

    /* Avoid empty batch calls (and also speed up matters) in the
       not-so-uncommon situation that we don't have any tracked transactions
       pending at the moment.  */
    if (pool.empty ())
      return {};

    for (const auto& txid : pool)
      {
        Json::Value params(Json::arrayValue);
        params.append ("0x" + txid);
        const int id = req.addCall ("eth_getTransactionByHash", params);
        idsWithTxid.emplace_back (id, txid);
      }
  }

  /* While the actual RPC call is running, we do not hold the lock to ensure
     parallel calls are fine.  In theory a new transaction could be added
     now or some removed by a parallel call, but that's fine.  */
  auto resp = rpc.CallProcedures (req);

  /* Transactions that come back with a null blockHash are still pending, those
     are the ones we return from the call.  Transactions that are not found
     or that have a non-null blockHash can be removed.

     Note that in case of a race condition, the returned transactions may not
     exactly match the current pool.  Transactions in the pool which are not
     in the RPC response will just be ignored here (neither returned from the
     method nor removed).  */
  std::vector<Json::Value> transactions;
  {
    std::lock_guard<std::mutex> lock(mut);
    for (const auto& entry : idsWithTxid)
      {
        Json::Value idVal(entry.first);
        const int err = resp.getErrorCode (idVal);
        CHECK_EQ (err, 0)
            << "Error " << err << " retrieving transaction data"
            << " for " << entry.second << ":\n"
            << resp.getErrorMessage (idVal);

        const auto txJson = resp.getResult (entry.first);
        if (txJson.isNull ())
          {
            VLOG (1)
                << "Transaction " << entry.second
                << " is unknown, removing from mempool";
            pool.erase (entry.second);
            continue;
          }

        CHECK (txJson.isObject ());
        CHECK_EQ (txJson["hash"].asString (), "0x" + entry.second);

        if (txJson["blockHash"].isNull ())
          {
            VLOG (2)
                << "Transaction " << entry.second << " is still pending";
            transactions.push_back (txJson);
          }
        else
          {
            VLOG (1)
                << "Transaction " << entry.second
                << " has been confirmed, removing from mempool";
            pool.erase (entry.second);
          }
      }
  }

  /* Order the transactions properly and return the final result.
     The important criterion is that we order within one sender address
     by nonce, and we do order by sender as well to ensure a well-defined
     result ordering.  */
  std::sort (transactions.begin (), transactions.end (), &IsEarlierForMempool);
  std::vector<std::string> res;
  for (const auto& tx : transactions)
    res.push_back (ConvertUint256 (tx["hash"].asString ()));

  return res;
}

/* ************************************************************************** */

} // namespace xayax
