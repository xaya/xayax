// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAX_PENDING_HPP
#define XAYAX_PENDING_HPP

#include "blockdata.hpp"
#include "private/zmqpub.hpp"

#include <mutex>
#include <string>
#include <vector>

namespace xayax
{

/**
 * Class that processes pending moves.  It applies some logic to synchronise
 * them with respect to TipChange notifications received and updates of
 * Chainstate tip to try and make sure all is done in order when pushing
 * ZMQ notifications to GSPs.
 *
 * In particular, one situation we want to properly handle is this:  When
 * the Chainstate tip is already synced up but the push notifications from
 * the BaseChain about new tips and pending moves are delayed, we don't want
 * to push pendings to the GSP that are actually already confirmed in the
 * later chain tip.
 *
 * The underlying assumption here is that while the Chainstate tip can
 * update out-of-order (e.g. from explicitly requested block ranges),
 * push notifications about TipChanged and pending moves are in order.
 */
class PendingManager
{

private:

  /** ZmqPub instance we use for pushing pendings.  */
  ZmqPub& zmq;

  /** Lock for this instance's data.  */
  std::mutex mut;

  /** Current tip of the Chainstate.  */
  std::string chainstateTip;

  /** Current / last tip sent with TipChanged notifications.  */
  std::string notificationTip;

  /**
   * Queued pending moves received since the last TipChanged.  They are
   * cleared if a new TipChanged is received (potentially dropping them,
   * but that is fine as pendings are best-effort only and this situation
   * only happens during initial sync / reorgs).  If the chainstate tip
   * catches up to the TipChanged tip, the queued notifications are pushed
   * then (as well as all received future pendings immediately).
   */
  std::vector<std::vector<MoveData>> pendingsQueue;

public:

  explicit PendingManager (ZmqPub& z)
    : zmq(z)
  {}

  /**
   * Handles a newly received TipChanged notification.
   */
  void TipChanged (const std::string& tip);

  /**
   * Handles a newly received notification about pending moves.
   */
  void PendingMoves (const std::vector<MoveData>& moves);

  /**
   * Handles an update in the chainstate's best tip.
   */
  void ChainstateTipChanged (const std::string& newTip);

};

} // namespace xayax

#endif // XAYAX_PENDING_HPP
