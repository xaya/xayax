// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAX_TESTUTILS_HPP
#define XAYAX_TESTUTILS_HPP

#include <json/json.h>

#include <string>

namespace xayax
{

/**
 * Parses a string as JSON, for use in testing when JSON values are needed.
 */
Json::Value ParseJson (const std::string& str);

} // namespace xayax

#endif // XAYAX_TESTUTILS_HPP
