// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAX_BLOCKDATA_HPP
#define XAYAX_BLOCKDATA_HPP

#include <json/json.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace xayax
{

/**
 * Data about a move (name update) taking place in the blockchain.
 */
struct MoveData
{

  /**
   * This move's transaction ID.  This is something that must be present
   * as such for every move and every base chain, as it is used by libxayagame
   * itself in the pending logic.
   */
  std::string txid;

  /** The namespace of the name being updated.  */
  std::string ns;

  /** The name (without namespace) being updated.  */
  std::string name;

  /** The raw move data (name value) as unparsed JSON string.  */
  std::string mv;

  /**
   * Information about CHI burns in that move, which is something
   * per-game (because the burn has to commit to a particular game ID).
   *
   * The ZMQ publisher picks out the right value from here based
   * on the game ID that it publishes for at the moment.
   */
  std::map<std::string, Json::Value> burns;

  /**
   * Other metadata (e.g. transferred coins) that is just stored
   * and forwarded to GSPs.  This includes everything which is not directly
   * parsed/processed by the SDK (i.e. libxayagame mostly) itself.
   */
  Json::Value metadata;

  MoveData () = default;
  MoveData (const MoveData&) = default;
  MoveData (MoveData&&) = default;

  MoveData& operator= (const MoveData&) = default;
  MoveData& operator= (MoveData&&) = default;

  friend bool
  operator== (const MoveData& a, const MoveData& b)
  {
    return a.txid == b.txid && a.ns == b.ns && a.name == b.name && a.mv == b.mv
            && a.burns == b.burns && a.metadata == b.metadata;
  }

  friend bool
  operator!= (const MoveData& a, const MoveData& b)
  {
    return !(a == b);
  }

  friend std::ostream&
  operator<< (std::ostream& out, const MoveData& x)
  {
    out << "Move " << x.txid << ":\n";
    out << "  " << x.ns << "/" << x.name << "\n";
    out << "  " << x.mv << "\n";
    out << "  with " << x.burns.size () << " burns\n";
    out << "  metadata:\n" << x.metadata;
    return out;
  }

};

/**
 * Basic data about a block.  This is a data container, which is used to
 * pass around blocks, e.g. from the blockchain interface to the chainstate
 * and from the chainstate to the ZMQ interface when a reorg happens.
 */
struct BlockData
{

  /** The block's hash.  */
  std::string hash;

  /** The block's parent hash.  */
  std::string parent;

  /** The block's height relative to the blockchain genesis.  */
  uint64_t height = 0;

  /**
   * The RNG seed value for this block.  This must be a hex string
   * representing an uint256.
   */
  std::string rngseed;

  /**
   * Other metadata (e.g. timestamps, RNG seed) that is just stored and
   * forwarded to the GSP.
   */
  Json::Value metadata;

  /** All moves inside this block.  */
  std::vector<MoveData> moves;

  BlockData () = default;
  BlockData (const BlockData&) = default;
  BlockData (BlockData&&) = default;

  BlockData& operator= (const BlockData&) = default;
  BlockData& operator= (BlockData&&) = default;

  friend bool operator== (const BlockData& a, const BlockData& b)
  {
    return a.hash == b.hash && a.parent == b.parent && a.height == b.height
            && a.rngseed == b.rngseed && a.metadata == b.metadata
            && a.moves == b.moves;
  }

  friend bool operator!= (const BlockData& a, const BlockData& b)
  {
    return !(a == b);
  }

};

} // namespace xayax

#endif // XAYAX_BLOCKDATA_HPP
