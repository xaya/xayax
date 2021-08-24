// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "abi.hpp"

#include "rpcutils.hpp"

#include <glog/logging.h>

namespace xayax
{

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

std::string
AbiDecoder::ReadString ()
{
  /* In the actual data stream we have just a pointer to the tail data
     where the real data for the string is.  */
  const size_t ptr = ParseInt (ReadUint (256));

  /* Construct a temporary new decoder instance that starts on this data,
     and use it to read the actual underlying string.  */
  AbiDecoder dec("0x" + data.substr (2 * ptr));

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

} // namespace xayax
