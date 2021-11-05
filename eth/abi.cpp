// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "abi.hpp"

#include "rpcutils.hpp"

#include "rpc-stubs/ethrpcclient.h"

#include <glog/logging.h>

namespace xayax
{

/* ************************************************************************** */

AbiDecoder::AbiDecoder (const std::string& str)
  : data(str.substr (2)), in(data)
{
  CHECK_EQ (str.substr (0, 2), "0x") << "Missing 0x prefix:\n" << str;
}

std::string
AbiDecoder::ReadBytes (const size_t len)
{
  std::string res(2 * len, '\0');
  in.read (&res[0], res.size ());
  CHECK (in) << "Error reading data, EOF?";
  return res;
}

std::string
AbiDecoder::ReadUint (const int bits)
{
  CHECK_EQ (bits % 8, 0) << "Invalid bit size: " << bits;
  const size_t numBytes = bits / 8;
  CHECK_LE (numBytes, 32) << "Max uint size is 256 bits";

  const std::string data256 = ReadBytes (32);
  const size_t expectedZeros = 2 * (32 - numBytes);
  CHECK_EQ (data256.substr (0, expectedZeros),
            std::string (expectedZeros, '0'));

  return "0x" + data256.substr (expectedZeros);
}

AbiDecoder
AbiDecoder::ReadDynamic ()
{
  /* In the actual data stream we have just a pointer to the tail data
     where the real data for the string is.  */
  const size_t ptr = ParseInt (ReadUint (256));

  return AbiDecoder ("0x" + data.substr (2 * ptr));
}

std::string
AbiDecoder::ReadString ()
{
  AbiDecoder dec = ReadDynamic ();
  const size_t len = ParseInt (dec.ReadUint (256));

  const std::string hexData = dec.ReadBytes (len);
  /* The data is padded on the right with zero bytes to make up
     for the total length being a multiple of 32 bytes.  */
  if (len % 32 != 0)
    {
      const size_t skipped = 32 - (len % 32);
      const std::string zeros = dec.ReadBytes (skipped);
      CHECK_EQ (zeros, std::string (2 * skipped, '0'))
          << "Padding is not just zeros";
    }

  std::string res;
  CHECK (Unhexlify (hexData, res));

  return res;
}

AbiDecoder
AbiDecoder::ReadArray (size_t& len)
{
  AbiDecoder dec = ReadDynamic ();
  len = ParseInt (dec.ReadUint (256));

  /* When the elements contain dynamic data, tail pointers in them
     are actually relative to the start of the elements data, not including
     the initial length.  */
  return AbiDecoder ("0x" + dec.data.substr (2 * 0x20));
}

int64_t
AbiDecoder::ParseInt (const std::string& str)
{
  const bool isHex = (str.substr (0, 2) == "0x");
  const std::string baseIn = str.substr (isHex ? 2 : 0);
  std::istringstream in(baseIn);

  int64_t res;
  if (isHex)
    in >> std::hex;
  in >> res;

  /* Verify that we did not overflow by encoding back to a string
     (perhaps with zero paddings) and checking it against the input.  */
  std::ostringstream out;
  if (isHex)
    out << std::hex;
  out << res;
  std::ostringstream fullOut;
  if (isHex)
    fullOut << "0x";
  if (out.str ().size () < baseIn.size ())
    fullOut << std::string (baseIn.size () - out.str ().size (), '0');
  fullOut << out.str ();
  CHECK_EQ (fullOut.str (), str) << "Integer overflow?";

  return res;
}

/* ************************************************************************** */

namespace
{

/**
 * Asserts that some string has a 0x prefix and strips it off.
 */
std::string
Strip0x (const std::string& str)
{
  CHECK_EQ (str.substr (0, 2), "0x") << "Missing hex prefix on " << str;
  return str.substr (2);
}

} // anonymous namespace

void
AbiEncoder::WriteWord (const std::string& data)
{
  const std::string plainData = Strip0x (ToLower (data));
  const int zeros = 2 * 32 - plainData.size ();
  CHECK_GE (zeros, 0) << "Word has more than 32 bytes already";
  head << std::string (zeros, '0') << plainData;
}

void
AbiEncoder::WriteBytes (const std::string& data)
{
  const std::string plainData = Strip0x (ToLower (data));

  CHECK_EQ (plainData.size () % 2, 0);
  const unsigned numBytes = plainData.size () / 2;

  CHECK_EQ (tail.str ().size () % 2, 0);
  const unsigned ptr = headWords * 32 + tail.str ().size () / 2;
  WriteWord (FormatInt (ptr));

  /* Construct a temporary second encoder that we use to write
     the actual data in the tail portion (length + bytes).  */
  AbiEncoder dataEnc(1);
  dataEnc.WriteWord (FormatInt (numBytes));
  dataEnc.tail << plainData;
  if (numBytes == 0 || numBytes % 32 > 0)
    dataEnc.tail << std::string (2 * (32 - (numBytes % 32)), '0');
  tail << Strip0x (dataEnc.Finalise ());
}

std::string
AbiEncoder::Finalise () const
{
  const std::string headStr = head.str ();
  CHECK_EQ (headStr.size (), 2 * 32 * headWords)
      << "Head words generated don't match the pre-set number";
  return "0x" + headStr + tail.str ();
}

std::string
AbiEncoder::ConcatHex (const std::string& a, const std::string& b)
{
  return "0x" + Strip0x (a) + Strip0x (b);
}

std::string
AbiEncoder::FormatInt (const uint64_t val)
{
  std::ostringstream out;
  out << std::hex << val;
  const std::string hexStr = out.str ();

  /* Make sure there is a full number of bytes at least in the string.  */
  std::ostringstream res;
  res << "0x";
  if (hexStr.size () % 2 > 0)
    res << '0';
  res << hexStr;

  return res.str ();
}

std::string
AbiEncoder::ToLower (const std::string& str)
{
  const std::string plain = Strip0x (str);

  std::string out = "0x";
  for (const char c : plain)
    out.push_back (std::tolower (c));

  return out;
}

/* ************************************************************************** */

std::string
GetEventTopic (EthRpcClient& rpc, const std::string& signature)
{
  return rpc.web3_sha3 ("0x" + Hexlify (signature));
}

} // namespace xayax
