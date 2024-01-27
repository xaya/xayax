// Copyright (C) 2021-2023 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "private/jsonutils.hpp"

#include <glog/logging.h>

#include <sstream>

namespace xayax
{

std::string
StoreJson (const Json::Value& val)
{
  Json::StreamWriterBuilder wbuilder;
  wbuilder["commentStyle"] = "None";
  wbuilder["indentation"] = "";
  wbuilder["enableYAMLCompatibility"] = false;
  wbuilder["dropNullPlaceholders"] = false;
  wbuilder["useSpecialFloats"] = false;

  return Json::writeString (wbuilder, val);
}

Json::Value
LoadJson (const std::string& str)
{
  Json::CharReaderBuilder rbuilder;
  rbuilder["allowComments"] = false;
  rbuilder["strictRoot"] = false;
  rbuilder["failIfExtra"] = true;
  rbuilder["rejectDupKeys"] = true;

  Json::Value res;
  std::string parseErrs;
  std::istringstream in(str);
  CHECK (Json::parseFromStream (rbuilder, in, &res, &parseErrs))
      << "Invalid JSON stored: " << parseErrs << "\n" << str;

  return res;
}

} // namespace xayax
