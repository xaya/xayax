// Copyright (C) 2023 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpcutils.hpp"

#include <glog/logging.h>

namespace xayax
{

RpcHeaders
ParseRpcHeaders (const std::string& str)
{
  RpcHeaders res;

  if (str.empty ())
    return res;

  size_t pos = 0;
  while (true)
    {
      size_t keyEnd = str.find ('=', pos);
      if (keyEnd == std::string::npos)
        {
          LOG (WARNING)
              << "Ignoring invalid tail for headers: "
              << str.substr (pos);
          return res;
        }

      size_t valueEnd = str.find (';', pos);
      if (valueEnd < keyEnd)
        {
          LOG (WARNING)
              << "Ignoring invalid tail for headers: "
              << str.substr (pos);
          return res;
        }
      CHECK_GT (valueEnd, keyEnd);

      const std::string key = str.substr (pos, keyEnd - pos);
      const std::string value = str.substr (keyEnd + 1, valueEnd - keyEnd - 1);
      res.emplace (key, value);

      if (valueEnd == std::string::npos)
        return res;

      pos = valueEnd + 1;
    }

  return res;
}

} // namespace xayax
