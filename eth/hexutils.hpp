// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAX_ETH_HEXUTILS_HPP
#define XAYAX_ETH_HEXUTILS_HPP

#include <string>

namespace xayax
{

/**
 * Converts a uint256 hash with 0x prefix as from the Ethereum RPC
 * interface to one without prefix as libxayagame expects them.
 */
std::string ConvertUint256 (const std::string& withPrefix);

} // namespace xayax

#endif // XAYAX_ETH_HEXUTILS_HPP
