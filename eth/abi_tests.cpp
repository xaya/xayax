// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "abi.hpp"

#include <gtest/gtest.h>

namespace xayax
{
namespace
{

/* ************************************************************************** */

using AbiDecoderTests = testing::Test;

TEST_F (AbiDecoderTests, ParseInt)
{
  EXPECT_EQ (AbiDecoder::ParseInt ("1234"), 1234);
  EXPECT_EQ (AbiDecoder::ParseInt ("0x0000"), 0);
  EXPECT_EQ (AbiDecoder::ParseInt ("0000"), 0);
  EXPECT_EQ (AbiDecoder::ParseInt ("0xffaa2"), 0xFFAA2);
  EXPECT_EQ (AbiDecoder::ParseInt ("0x00001234"), 0x1234);
}

TEST_F (AbiDecoderTests, DecodeMoveEvent)
{
  /* This is real event data from a move transaction on the Mumbai testnet:
     0x9bafd026d0badd9518d497dde95172ae306848790348de95e2928aecbcfda72d  */
  AbiDecoder dec("0x"
      "00000000000000000000000000000000000000000000000000000000000000e0"
      "0000000000000000000000000000000000000000000000000000000000000120"
      "0000000000000000000000000000000000000000000000000000000000000160"
      "0000000000000000000000000000000000000000000000000000000000000002"
      "00000000000000000000000014e663e1531e0f438840952d18720c74c28d4f20"
      "00000000000000000000000000000000000000000000000000000000000004d2"
      "000000000000000000000000f0534cc8f4c22972d31105c7ac7b656b581a3a8e"
      "0000000000000000000000000000000000000000000000000000000000000001"
      "7000000000000000000000000000000000000000000000000000000000000000"
      "0000000000000000000000000000000000000000000000000000000000000005"
      "646f6d6f62000000000000000000000000000000000000000000000000000000"
      "0000000000000000000000000000000000000000000000000000000000000002"
      "7b7d000000000000000000000000000000000000000000000000000000000000");

  ASSERT_EQ (dec.ReadString (), "p");
  ASSERT_EQ (dec.ReadString (), "domob");
  ASSERT_EQ (dec.ReadString (), "{}");
  ASSERT_EQ (AbiDecoder::ParseInt (dec.ReadUint (256)), 2);
  ASSERT_EQ (dec.ReadUint (160), "0x14e663e1531e0f438840952d18720c74c28d4f20");
  ASSERT_EQ (AbiDecoder::ParseInt (dec.ReadUint (256)), 1234);
  ASSERT_EQ (dec.ReadUint (160), "0xf0534cc8f4c22972d31105c7ac7b656b581a3a8e");
}

/* ************************************************************************** */

using AbiEncoderTests = testing::Test;

TEST_F (AbiEncoderTests, FormatInt)
{
  EXPECT_EQ (AbiEncoder::FormatInt (0), "0x00");
  EXPECT_EQ (AbiEncoder::FormatInt (255), "0xff");
  EXPECT_EQ (AbiEncoder::FormatInt (256), "0x0100");
  EXPECT_EQ (AbiEncoder::FormatInt (0x1234abcd), "0x1234abcd");
}

TEST_F (AbiEncoderTests, ConcatHex)
{
  EXPECT_EQ (AbiEncoder::ConcatHex ("0x", "0x"), "0x");
  EXPECT_EQ (AbiEncoder::ConcatHex ("0x", "0x10"), "0x10");
  EXPECT_EQ (AbiEncoder::ConcatHex ("0x10", "0x"), "0x10");
  EXPECT_EQ (AbiEncoder::ConcatHex ("0x1234", "0xbbcc"), "0x1234bbcc");
  EXPECT_DEATH (AbiEncoder::ConcatHex ("1234", "0xbbcc"), "Missing hex prefix");
  EXPECT_DEATH (AbiEncoder::ConcatHex ("0x1234", "bbcc"), "Missing hex prefix");
}

TEST_F (AbiEncoderTests, ToLower)
{
  EXPECT_EQ (AbiEncoder::ToLower ("0x12abDe"), "0x12abde");
}

TEST_F (AbiEncoderTests, ForwarderExecute)
{
  /* These are real ABI-encoded arguments for the CallForwarder.execute
     method and different sizes of data.  Those are what we will need
     the encoding for mostly.  */

  const std::string addr = "0xB18947C38B180A0A162b14ddD09597ac43e931Fb";
  const auto encode = [&] (const std::string& data)
    {
      AbiEncoder enc(2);
      enc.WriteWord (addr);
      enc.WriteBytes (data);
      return enc.Finalise ();
    };

  EXPECT_EQ (encode ("0x"), "0x"
      "000000000000000000000000b18947c38b180a0a162b14ddd09597ac43e931fb"
      "0000000000000000000000000000000000000000000000000000000000000040"
      "0000000000000000000000000000000000000000000000000000000000000000"
      "0000000000000000000000000000000000000000000000000000000000000000");
  EXPECT_EQ (encode ("0x1122"), "0x"
      "000000000000000000000000b18947c38b180a0a162b14ddd09597ac43e931fb"
      "0000000000000000000000000000000000000000000000000000000000000040"
      "0000000000000000000000000000000000000000000000000000000000000002"
      "1122000000000000000000000000000000000000000000000000000000000000");
  std::string fullWord;
  for (unsigned i = 0; i < 32; ++i)
    fullWord += "42";
  EXPECT_EQ (encode ("0x" + fullWord), "0x"
      "000000000000000000000000b18947c38b180a0a162b14ddd09597ac43e931fb"
      "0000000000000000000000000000000000000000000000000000000000000040"
      "0000000000000000000000000000000000000000000000000000000000000020"
      "4242424242424242424242424242424242424242424242424242424242424242");
  EXPECT_EQ (encode ("0xab" + fullWord), "0x"
      "000000000000000000000000b18947c38b180a0a162b14ddd09597ac43e931fb"
      "0000000000000000000000000000000000000000000000000000000000000040"
      "0000000000000000000000000000000000000000000000000000000000000021"
      "ab42424242424242424242424242424242424242424242424242424242424242"
      "4200000000000000000000000000000000000000000000000000000000000000");
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace xayax
