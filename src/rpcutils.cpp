// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpcutils.hpp"

#include <glog/logging.h>

#include <iomanip>
#include <sstream>

namespace xayax
{

std::string
Hexlify (const std::string& bin)
{
  std::ostringstream out;
  out << std::hex;
  for (const char c : bin)
    {
      const int val = static_cast<int> (static_cast<uint8_t> (c));
      CHECK_GE (val, 0);
      CHECK_LE (val, 0xFF);
      if (val < 0x10)
        out << '0';
      out << val;
    }
  return out.str ();
}

bool
Unhexlify (const std::string& hex, std::string& bin)
{
  if (hex.size () % 2 != 0)
    {
      LOG (WARNING) << "Hex string has size " << hex.size ();
      return false;
    }

  bin.clear ();
  for (size_t i = 0; i < hex.size (); i += 2)
    {
      std::istringstream in(hex.substr (i, 2));
      int val;
      in >> std::hex >> val;
      bin.push_back (static_cast<char> (val));
    }

  const std::string rehex = Hexlify (bin);
  if (hex != rehex)
    {
      LOG (WARNING)
          << "Mismatch between parsed string in hex and input:\n"
          << rehex << "\n" << hex;
      return false;
    }

  return true;
}

} // namespace xayax
