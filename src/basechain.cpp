// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "basechain.hpp"

namespace xayax
{

void
BaseChain::SetCallbacks (Callbacks* c)
{
  cb = c;
}

void
BaseChain::TipChanged ()
{
  if (cb != nullptr)
    cb->TipChanged ();
}

} // namespace xayax
