// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "basechain.hpp"

namespace xayax
{

void
BaseChain::SetCallbacks (Callbacks* c)
{
  std::lock_guard<std::mutex> lock(mut);
  cb = c;
}

void
BaseChain::TipChanged (const std::string& tip)
{
  std::lock_guard<std::mutex> lock(mut);
  if (cb != nullptr)
    cb->TipChanged (tip);
}

void
BaseChain::PendingMoves (const std::vector<MoveData>& moves)
{
  std::lock_guard<std::mutex> lock(mut);
  if (cb != nullptr)
    cb->PendingMoves (moves);
}

} // namespace xayax
