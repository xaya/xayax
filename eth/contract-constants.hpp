// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAX_ETH_CONTRACT_CONSTANTS_HPP
#define XAYAX_ETH_CONTRACT_CONSTANTS_HPP

#include <string>

namespace xayax
{

/** The hex hash value of the Solidity move event topic.  */
extern const std::string MOVE_EVENT;

/** The function selector for XayaAccount's wchiToken() method.  */
extern const std::string ACCOUNT_WCHI_FCN;
/** The function selector for CallForwarder::execute.  */
extern const std::string FORWARDER_EXECUTE_FCN;

/** The bytecode for deploying the CallForwarder contract.  */
extern const std::string CALL_FORWARDER_CODE;
/** The bytecode for deploying the TrackingAccounts contract.  */
extern const std::string TRACKING_ACCOUNTS_CODE;

} // namespace xayax

#endif // XAYAX_ETH_CONTRACT_CONSTANTS_HPP
