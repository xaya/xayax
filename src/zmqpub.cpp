// Copyright (C) 2021-2024 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "private/zmqpub.hpp"

#include <univalue.h>

#include <glog/logging.h>

#include <map>
#include <sstream>

namespace xayax
{

namespace
{

/** High-water mark used for sending.  */
constexpr int SEND_HWM = 1'000;

/** Topic prefix for block-attach messages.  */
constexpr const char* PREFIX_ATTACH = "game-block-attach";
/** Topic prefix for block-detach messages.  */
constexpr const char* PREFIX_DETACH = "game-block-detach";

/** Topic prefix for pending moves.  */
constexpr const char* PREFIX_MOVE = "game-pending-move";

/**
 * Tries to parse a given string of move data as JSON.  Returns true
 * if parsing was successful and the move is considered valid.
 */
bool
ParseMoveJson (const std::string& txid, const std::string& str,
               Json::Value& val)
{
  /* Moves are the main user-provided input that we have to be very careful
     in processing.  Univalue is used as the first line of defence in parsing
     JSON from moves in Xaya Core as well, and it seems like a robust and
     stable library for this task.  Thus we filter the move data through
     Univalue first, before passing it on to jsoncpp.  */
  UniValue value;
  if (!value.read (str) || !value.isObject ())
    {
      LOG (WARNING) << "Move data for " << txid << " is invalid JSON:\n" << str;
      return false;
    }
  const std::string filtered = value.write ();

  Json::CharReaderBuilder rbuilder;
  rbuilder["allowComments"] = false;
  rbuilder["strictRoot"] = true;
  rbuilder["failIfExtra"] = true;
  /* Univalue accepts duplicate keys, so it may forward moves to
     us that contain duplicate keys.  We need to handle them gracefully when
     parsing.  Specifically, we reject them, but just fail (instead of
     aborting) and ignore this move.  */
  rbuilder["rejectDupKeys"] = true;

  std::string parseErrs;
  std::istringstream in(filtered);
  if (!Json::parseFromStream (rbuilder, in, &val, &parseErrs))
    {
      LOG (WARNING)
          << "Invalid JSON produced by Univalue (dup keys?): "
          << parseErrs << "\n" << filtered;
      return false;
    }

  return true;
}

/**
 * Extracts the metadata field of a block or move (that we got from the
 * base-chain connector directly).  If the field is set to an object, we
 * return it directly.  If it is null, we return an empty object.  Any other
 * value is invalid and aborts.
 */
template <typename T>
  Json::Value
  InitFromMetadata (const T& data)
{
  if (data.metadata.isNull ())
    return Json::Value (Json::objectValue);
  if (data.metadata.isObject ())
    return data.metadata;
  LOG (FATAL) << "Invalid metadata from base chain:\n" << data.metadata;
}

/**
 * Adds burn data to a move JSON.
 */
void
AddBurnData (const MoveData& mv, const std::string& gameId, Json::Value& val)
{
  const auto mit = mv.burns.find (gameId);
  if (mit == mv.burns.end ())
    val["burnt"] = 0;
  else
    val["burnt"] = mit->second;
}

/**
 * Helper class that analyses a single move and extracts the data from it
 * that is relevant for our notifications.
 */
class PerTxData
{

private:

  /**
   * Type for a map that holds the actual move data per each game that
   * was part of a transaction.
   */
  using MovesPerGame = std::map<std::string, Json::Value>;

  /**
   * Move data in this tx for each game.  This is already the full JSON that
   * will be pushed by ZMQ, including the txid, name and metadata.
   */
  MovesPerGame moves;

  /** Set to true if this is an admin command.  */
  bool isAdmin = false;
  /** If this is an admin command, the associated game ID.  */
  std::string adminGame;
  /**
   * If this is an admin command, the command data.  This is already the
   * full JSON that will be pushed, including the tx metadata.
   */
  Json::Value adminCmd;

public:

  /**
   * Constructs this by analysing a given move.
   */
  explicit PerTxData (const MoveData& mv);

  PerTxData () = delete;
  PerTxData (const PerTxData&) = delete;
  void operator= (const PerTxData&) = delete;

  const MovesPerGame&
  GetMovesPerGame () const
  {
    return moves;
  }

  /**
   * Returns true if this is an admin command and sets the game ID
   * and command data.
   */
  bool
  GetAdminCommand (std::string& game, Json::Value& cmd) const
  {
    if (isAdmin)
      {
        game = adminGame;
        cmd = adminCmd;
      }
    return isAdmin;
  }

};

PerTxData::PerTxData (const MoveData& mv)
{
  Json::Value value;
  if (!ParseMoveJson (mv.txid, mv.mv, value))
    {
      /* This is not a valid move.  Just leave isAdmin as false and
         the per-game move array as empty.  */
      return;
    }
  CHECK (value.isObject ());

  /* Build up the base metadata template for this transaction.  */
  Json::Value txTemplate = InitFromMetadata (mv);
  txTemplate["txid"] = mv.txid;

  /* Handle a potential admin command.  */
  if (mv.ns == "g")
    {
      if (value.isMember ("cmd"))
        {
          isAdmin = true;
          adminGame = mv.name;

          adminCmd = txTemplate;
          adminCmd["cmd"] = value["cmd"];
          AddBurnData (mv, adminGame, adminCmd);
        }
      return;
    }

  /* Otherwise we are only interested in player moves.  */
  if (mv.ns != "p")
    return;
  const auto& g = value["g"];
  if (!g.isObject ())
    return;

  /* Insert each game into our array of per-game moves.  */
  txTemplate["name"] = mv.name;
  for (auto it = g.begin (); it != g.end (); ++it)
    {
      CHECK (it.key ().isString ());
      const std::string gameId = it.key ().asString ();

      Json::Value thisGame = txTemplate;
      thisGame["move"] = *it;
      AddBurnData (mv, gameId, thisGame);

      CHECK (moves.emplace (gameId, thisGame).second)
          << "We already have move data for " << gameId;
    }
}

} // anonymous namespace

ZmqPub::ZmqPub (const std::string& addr)
  : sock(ctx, zmq::socket_type::pub)
{
  LOG (INFO) << "Binding ZMQ publisher to " << addr;
  sock.set (zmq::sockopt::sndhwm, SEND_HWM);
  sock.set (zmq::sockopt::tcp_keepalive, 1);
  sock.bind (addr);
}

ZmqPub::~ZmqPub ()
{
  std::lock_guard<std::mutex> lock(mut);

  /* Make sure we close the socket right away.  */
  sock.set (zmq::sockopt::linger, 0);
  sock.close ();
}

void
ZmqPub::TrackGame (const std::string& g)
{
  std::lock_guard<std::mutex> lock(mut);

  uint64_t newDepth;
  auto mit = games.find (g);
  if (mit == games.end ())
    {
      games.emplace (g, 1);
      newDepth = 1;
    }
  else
    {
      newDepth = mit->second + 1;
      mit->second = newDepth;
    }

  LOG (INFO) << "Tracking game '" << g << "', new depth: " << newDepth;
}

void
ZmqPub::UntrackGame (const std::string& g)
{
  std::lock_guard<std::mutex> lock(mut);

  uint64_t newDepth;
  auto mit = games.find (g);
  if (mit == games.end ())
    newDepth = 0;
  else
    {
      CHECK_GT (mit->second, 0);
      newDepth = mit->second - 1;

      if (newDepth == 0)
        games.erase (mit);
      else
        mit->second = newDepth;
    }

  LOG (INFO) << "Untracking game '" << g << "', new depth: " << newDepth;
}

void
ZmqPub::SendMessage (const std::string& cmd, const Json::Value& data)
{
  auto mitSeq = nextSeq.find (cmd);
  if (mitSeq == nextSeq.end ())
    mitSeq = nextSeq.emplace (cmd, 0).first;

  uint32_t seq = mitSeq->second;
  uint8_t seqBytes[sizeof (seq)];
  for (unsigned i = 0; i < sizeof (seq); ++i)
    {
      seqBytes[i] = seq & 0xFF;
      seq >>= 8;
    }
  CHECK_EQ (seq, 0);

  Json::StreamWriterBuilder wbuilder;
  wbuilder["commentStyle"] = "None";
  wbuilder["indentation"] = "";
  wbuilder["enableYAMLCompatibility"] = false;
  wbuilder["dropNullPlaceholders"] = false;
  wbuilder["useSpecialFloats"] = false;
  const std::string dataStr = Json::writeString (wbuilder, data);

  /* We want to handle EAGAIN in the same way as other errors.  */
  if (!sock.send (zmq::message_t (cmd), zmq::send_flags::sndmore))
    throw zmq::error_t ();

  VLOG (1) << "Sent ZMQ message: " << cmd;
  VLOG (2) << "Payload data:\n" << data;

  /* Once the first send succeeded, ZMQ guarantees atomic delivery of
     the further parts.  */
  CHECK (sock.send (zmq::message_t (dataStr), zmq::send_flags::sndmore));
  CHECK (sock.send (zmq::message_t (seqBytes, sizeof (seq)),
                    zmq::send_flags::none));

  /* Increase the sequence number at the end.  If the sending fails and
     throws, we want to keep the previous one.  */
  ++mitSeq->second;
}

void
ZmqPub::SendBlock (const std::string& cmdPrefix, const BlockData& blk,
                   const std::string& reqtoken)
{
  std::lock_guard<std::mutex> lock(mut);

  /* Prepare the template object for this block that is the same for each game
     we track.  */
  Json::Value blkJson = InitFromMetadata (blk);
  blkJson["hash"] = blk.hash;
  blkJson["parent"] = blk.parent;
  blkJson["height"] = static_cast<Json::Int64> (blk.height);
  blkJson["rngseed"] = blk.rngseed;
  Json::Value blkTemplate(Json::objectValue);
  blkTemplate["block"] = blkJson;
  if (!reqtoken.empty ())
    blkTemplate["reqtoken"] = reqtoken;

  /* Start with an empty array of moves and commands for every game
     that we track.  */
  std::map<std::string, Json::Value> perGameMoves;
  std::map<std::string, Json::Value> perGameAdmin;
  for (const auto& entry : games)
    {
      CHECK_GT (entry.second, 0);
      perGameMoves.emplace (entry.first, Json::Value (Json::arrayValue));
      perGameAdmin.emplace (entry.first, Json::Value (Json::arrayValue));
    }

  /* Process all moves in the block and add relevant data to the per-game
     arrays.  */
  for (const auto& mv : blk.moves)
    {
      const PerTxData data(mv);

      for (const auto& entry : data.GetMovesPerGame ())
        {
          const auto mit = perGameMoves.find (entry.first);
          if (mit == perGameMoves.end ())
            continue;

          CHECK_EQ (games.count (entry.first), 1);
          CHECK (mit->second.isArray ());
          mit->second.append (entry.second);
        }

      std::string adminGame;
      Json::Value adminCmd;
      if (data.GetAdminCommand (adminGame, adminCmd))
        {
          const auto mit = perGameAdmin.find (adminGame);
          if (mit == perGameAdmin.end ())
            continue;

          CHECK_EQ (games.count (adminGame), 1);
          CHECK (mit->second.isArray ());
          mit->second.append (adminCmd);
        }
    }

  /* Send out notifications for all tracked games.  */
  for (const auto& entry : games)
    {
      CHECK_GT (entry.second, 0);

      const auto mitMv = perGameMoves.find (entry.first);
      CHECK (mitMv != perGameMoves.end ());
      CHECK (mitMv->second.isArray ());

      const auto mitCmd = perGameAdmin.find (entry.first);
      CHECK (mitCmd != perGameAdmin.end ());
      CHECK (mitCmd->second.isArray ());

      Json::Value thisGame = blkTemplate;
      thisGame["moves"] = mitMv->second;
      thisGame["admin"] = mitCmd->second;

      SendMessage (cmdPrefix + " json " + entry.first, thisGame);
    }
}

void
ZmqPub::SendBlockAttach (const BlockData& blk, const std::string& reqtoken)
{
  VLOG (1) << "Block attach: " << blk.hash;
  SendBlock (PREFIX_ATTACH, blk, reqtoken);
}

void
ZmqPub::SendBlockDetach (const BlockData& blk, const std::string& reqtoken)
{
  VLOG (1) << "Block detach: " << blk.hash;
  SendBlock (PREFIX_DETACH, blk, reqtoken);
}

void
ZmqPub::SendPendingMoves (const std::vector<MoveData>& moves)
{
  CHECK (!moves.empty ());
  VLOG (1) << "Pending moves for transaction: " << moves.front ().txid;
  std::lock_guard<std::mutex> lock(mut);

  /* We start with an empty array of moves for each game that we track.  */
  std::map<std::string, Json::Value> movesPerGame;
  for (const auto& entry : games)
    {
      CHECK_GT (entry.second, 0);
      movesPerGame.emplace (entry.first, Json::Value (Json::arrayValue));
    }

  /* Process all the MoveData instances, adding to the list of moves
     per game.  */
  std::string lastTxid;
  for (const auto& mv : moves)
    {
      if (!lastTxid.empty ())
        CHECK_EQ (lastTxid, mv.txid) << "All moves must be from the same txid";
      lastTxid = mv.txid;

      const PerTxData data(mv);
      for (const auto& entry : data.GetMovesPerGame ())
        {
          const auto mit = movesPerGame.find (entry.first);
          if (mit == movesPerGame.end ())
            continue;

          CHECK_EQ (games.count (entry.first), 1);
          mit->second.append (entry.second);
        }
    }

  /* Send out all the notifications.  */
  for (const auto& entry : movesPerGame)
    if (entry.second.size () > 0)
      SendMessage (PREFIX_MOVE + (" json " + entry.first), entry.second);
}

} // namespace xayax
