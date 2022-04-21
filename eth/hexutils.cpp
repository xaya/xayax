// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "hexutils.hpp"

#include <glog/logging.h>

namespace xayax
{

std::string
ConvertUint256 (const std::string& withPrefix)
{
  CHECK_EQ (withPrefix.substr (0, 2), "0x");
  return withPrefix.substr (2);
}

} // namespace xayax
