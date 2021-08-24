// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpcutils.hpp"

#include <gtest/gtest.h>

namespace xayax
{
namespace
{

using HexlifyTests = testing::Test;

TEST_F (HexlifyTests, ValidRoundtrip)
{
  std::string actual;
  ASSERT_TRUE (Unhexlify ("00ff20780f1000", actual));
  ASSERT_EQ (actual.size (), 7);
  EXPECT_EQ (actual[0], '\0');
  EXPECT_EQ (actual[1], '\xFF');
  EXPECT_EQ (actual[2], ' ');
  EXPECT_EQ (actual[3], 'x');
  EXPECT_EQ (actual[4], '\x0F');
  EXPECT_EQ (actual[5], '\x10');
  EXPECT_EQ (actual[6], '\0');
  EXPECT_EQ (Hexlify (actual), "00ff20780f1000");

  ASSERT_TRUE (Unhexlify ("", actual));
  EXPECT_EQ (actual, "");
  EXPECT_EQ (Hexlify (actual), "");
}

TEST_F (HexlifyTests, UnhexlifyWrongSize)
{
  std::string actual;
  EXPECT_FALSE (Unhexlify ("a", actual));
}

TEST_F (HexlifyTests, UnhexlifyInvalidHex)
{
  std::string actual;
  EXPECT_FALSE (Unhexlify ("20x1", actual));
}

} // anonymous namespace
} // namespace xayax
