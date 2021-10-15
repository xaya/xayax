// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAX_ETH_ABI_HPP
#define XAYAX_ETH_ABI_HPP

#include <cstdint>
#include <sstream>
#include <string>

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

  AbiDecoder (AbiDecoder&) = delete;
  void operator= (AbiDecoder&) = delete;

  AbiDecoder (AbiDecoder&&) = default;
  AbiDecoder& operator= (AbiDecoder&&) = default;

  /**
   * Reads a blob of fixed bit size (e.g. uint256 or address/uint160).
   * It is returned as hex string with 0x prefix again.
   */
  std::string ReadUint (int bits);

  /**
   * Reads a generic dynamic piece of data.  This returns a new AbiDecoder
   * instance that is based on the tail data.
   */
  AbiDecoder ReadDynamic ();

  /**
   * Reads in a string value into a (potentially binary) string.
   */
  std::string ReadString ();

  /**
   * Reads a dynamic array.  It sets the length in the output argument,
   * and returns a new decoder that will return the elements one by one.
   */
  AbiDecoder ReadArray (size_t& len);

  /**
   * Parses a string (hex or decimal) as integer, verifying that
   * it fits into int64_t.
   */
  static int64_t ParseInt (const std::string& str);

};

/**
 * Helper class for encoding data into an ABI blob (hex string).
 */
class AbiEncoder
{

private:

  /**
   * The expected number of words (32-byte groups) in the heads part.
   * For simplicity, this must be set beforehand when constructing the
   * encoder, is used for constructing the tail references for dynamic
   * types, and verified at the end against the actual head generated.
   */
  const unsigned headWords;

  /** The stream of head data being written.  */
  std::ostringstream head;

  /** The stream of tail data being written.  */
  std::ostringstream tail;

public:

  /**
   * Constructs a new AbiEncoder instance that is supposed to write the
   * given number of words on the head part.
   */
  explicit AbiEncoder (unsigned w)
    : headWords(w)
  {}

  /**
   * Writes a word of uint data, which will be padded to 32 bytes with
   * zeros as needed.
   */
  void WriteWord (const std::string& data);

  /**
   * Writes the given data as a dynamic "bytes" instance.
   */
  void WriteBytes (const std::string& data);

  /**
   * Constructs the final string.  Exactly the right number of head words
   * must have been constructed.
   */
  std::string Finalise () const;

  /**
   * Concatenates two 0x-prefixed hex strings.
   */
  static std::string ConcatHex (const std::string& a, const std::string& b);

  /**
   * Formats a given integer as hex literal suitable to be written
   * with WriteWord (for instance).
   */
  static std::string FormatInt (uint64_t val);

  /**
   * Converts a hex string to all lower-case.
   */
  static std::string ToLower (const std::string& str);

};

} // namespace xayax

#endif // XAYAX_ETH_ABI_HPP
