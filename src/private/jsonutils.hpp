// Copyright (C) 2023 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAX_JSONUTILS_HPP
#define XAYAX_JSONUTILS_HPP

#include <json/json.h>

#include <string>

namespace xayax
{

/**
 * Converts a JSON value to a serialised string, in the way we do that for
 * storing JSON into e.g. a database.
 */
std::string StoreJson (const Json::Value& val);

/**
 * Tries to parse JSON from a string that was stored in a database or otherwise
 * saved with StoreJson previously.  CHECK fails if it is invalid.
 */
Json::Value LoadJson (const std::string& str);

} // namespace xayax

#endif // XAYAX_JSONUTILS_HPP
