// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAX_ETH_ABI_HPP
#define XAYAX_ETH_ABI_HPP

#include <cstdint>
#include <sstream>
#include <string>

class EthRpcClient;

namespace xayax
{

/**
 * Helper class for decoding data from an ABI-encoded hex string.
 */
class AbiDecoder
{

private:

  /** The input data being read (as hex string).  */
  const std::string data;

  /** The stream of input data.  */
  std::istringstream in;

  /**
   * Reads the given number of bytes as hex characters (i.e. 2n characters)
   * from the input stream and returns them as hex string.
   */
  std::string ReadBytes (size_t len);

public:

  explicit AbiDecoder (const std::string& str);

  /**
   * Reads a blob of fixed bit size (e.g. uint256 or address/uint160).
   * It is returned as hex string with 0x prefix again.
   */
  std::string ReadUint (int bits);

  /**
   * Reads in a string value into a (potentially binary) string.
   */
  std::string ReadString ();

  /**
   * Parses a string (hex or decimal) as integer, verifying that
   * it fits into int64_t.
   */
  static int64_t ParseInt (const std::string& str);

};

/**
 * Returns the topic value (as hex string) of a Solidity event with
 * the given signature.
 */
std::string GetEventTopic (EthRpcClient& rpc, const std::string& signature);

} // namespace xayax

#endif // XAYAX_ETH_ABI_HPP
