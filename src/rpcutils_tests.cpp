// Copyright (C) 2023 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpcutils.hpp"

#include <gtest/gtest.h>

namespace xayax
{
namespace
{

class ParseRpcHeadersTests : public testing::Test
{

protected:

  /** 
   * Parses a string and expects the given elements.
   */
  static void
  Expect (const std::string& str, const RpcHeaders& expected)
  {
    EXPECT_EQ (ParseRpcHeaders (str), expected);
  }

};

TEST_F (ParseRpcHeadersTests, EmptyString)
{
  Expect ("", {});
}

TEST_F (ParseRpcHeadersTests, SingleElement)
{
  Expect ("Key=Value", {{"Key", "Value"}});
}

TEST_F (ParseRpcHeadersTests, MultipleElements)
{
  Expect ("1=4;abc=xyz;foo=bar", {{"1", "4"}, {"abc", "xyz"}, {"foo", "bar"}});
}

TEST_F (ParseRpcHeadersTests, EmptyKeyOrValue)
{
  Expect ("=abc", {{"", "abc"}});
  Expect ("abc=", {{"abc", ""}});
  Expect ("=", {{"", ""}});
}

TEST_F (ParseRpcHeadersTests, InvalidTailIgnored)
{
  Expect ("abc=xyz;foo", {{"abc", "xyz"}});
  Expect ("abc=xyz;foo;1=2", {{"abc", "xyz"}});
}

} // anonymous namespace
} // namespace xayax
