// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "private/pending.hpp"

#include <glog/logging.h>

namespace xayax
{

void
PendingManager::TipChanged (const std::string& tip)
{
  std::lock_guard<std::mutex> lock(mut);

  LOG_IF (WARNING, !pendingsQueue.empty ())
      << "Dropping " << pendingsQueue.size ()
      << " queued pending moves for new tip change";

  pendingsQueue.clear ();
  notificationTip = tip;
  CHECK (!notificationTip.empty ());
}

void
PendingManager::PendingMoves (const std::vector<MoveData>& moves)
{
  std::lock_guard<std::mutex> lock(mut);

  /* Until we receive a first tip update, just drop everything.  We don't
     know anything about our state with respect to blocks, so better not try
     and send any pending moves.  */
  if (chainstateTip.empty ())
    {
      LOG (WARNING) << "Ignoring pending moves before first tip update";
      return;
    }

  /* If we are synced up, just send the moves right away.  */
  if (chainstateTip == notificationTip)
    {
      CHECK (pendingsQueue.empty ());
      zmq.SendPendingMoves (moves);
      return;
    }

  /* Otherwise add to the queue, so we can potentially send it later when
     the chainstate catches up.  */
  pendingsQueue.push_back (moves);
}

void
PendingManager::ChainstateTipChanged (const std::string& newTip)
{
  std::lock_guard<std::mutex> lock(mut);

  chainstateTip = newTip;
  CHECK (!chainstateTip.empty ());

  /* If we are caught up now, send all queued moves.  */
  if (chainstateTip == notificationTip)
    {
      LOG_IF (INFO, !pendingsQueue.empty ())
          << "Sending " << pendingsQueue.size ()
          << " previously queued pending moves";
      for (const auto& mv : pendingsQueue)
        zmq.SendPendingMoves (mv);
      pendingsQueue.clear ();
    }
}

} // namespace xayax
