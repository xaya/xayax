// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAX_ZMQPUB_HPP
#define XAYAX_ZMQPUB_HPP

#include "blockdata.hpp"

#include <json/json.h>
#include <zmq.hpp>

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace xayax
{

/**
 * ZMQ publisher that can push block and move data per the Xaya ZMQ spec:
 * https://github.com/xaya/xaya/blob/master/doc/xaya/interface.md
 */
class ZmqPub
{

private:

  zmq::context_t ctx;
  zmq::socket_t sock;

  /**
   * Lock for this instance, mainly for the ZMQ socket and the in-memory map
   * of sequence numbers).
   */
  std::mutex mut;

  /** Next sequence number per command string.  */
  std::unordered_map<std::string, uint32_t> nextSeq;

  /** Set of tracked game IDs.  */
  std::unordered_set<std::string> games;

  /**
   * Sends a multipart message consisting of command, JSON data and the right
   * sequence number.  The caller must ensure that all locks are held
   * as required.
   */
  void SendMessage (const std::string& cmd, const Json::Value& data);

  /**
   * Sends notifications for all tracked games for the given block, which is
   * either being detached or attached (and the "cmdPrefix" must be set
   * accordingly).
   */
  void SendBlock (const std::string& cmdPrefix, const BlockData& blk,
                  const std::string& reqtoken);

public:

  /**
   * Constructs the publisher, binding to the given address.
   */
  explicit ZmqPub (const std::string& addr);

  /**
   * Stops the publisher and cleans up the connection.
   */
  ~ZmqPub ();

  /**
   * Adds a game to the list of tracked games.
   */
  void TrackGame (const std::string& g);

  /**
   * Removes a game from the list of tracked games.
   */
  void UntrackGame (const std::string& g);

  /**
   * Pushes notifications for all tracked games and the given block
   * being attached.  If reqtoken is not empty, it will explicitly be set
   * in the notifications.
   */
  void SendBlockAttach (const BlockData& blk, const std::string& reqtoken);

  /**
   * Pushes notifications for all tracked games and the given block
   * being detached.
   */
  void SendBlockDetach (const BlockData& blk, const std::string& reqtoken);

};

} // namespace xayax

#endif // XAYAX_ZMQPUB_HPP
