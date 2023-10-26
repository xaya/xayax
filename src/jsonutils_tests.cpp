// Copyright (C) 2023 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "private/jsonutils.hpp"

#include <gtest/gtest.h>

namespace xayax
{
namespace
{

using JsonUtilsTests = testing::Test;

TEST_F (JsonUtilsTests, RoundTrip)
{
  for (const std::string str : {"0", "\"abc\"", "[1,2,3]", "{\"foo\":42}"})
    EXPECT_EQ (StoreJson (LoadJson (str)), str);
}

TEST_F (JsonUtilsTests, Invalid)
{
  EXPECT_DEATH (LoadJson ("foo"), "Invalid JSON stored");
}

} // anonymous namespace
} // namespace xayax
