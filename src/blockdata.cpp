// Copyright (C) 2023 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockdata.hpp"

#include "private/jsonutils.hpp"
#include "proto/blockdata.pb.h"

#include <glog/logging.h>

namespace xayax
{

std::ostream&
operator<< (std::ostream& out, const MoveData& x)
{
  out << "Move " << x.txid << ":\n";
  out << "  " << x.ns << "/" << x.name << "\n";
  out << "  " << x.mv << "\n";
  out << "  with " << x.burns.size () << " burns\n";
  out << "  metadata:\n" << x.metadata;
  return out;
}

std::string
BlockData::Serialise () const
{
  /* We use protocol buffers internally to implement the serialisation,
     but this is not exposed on the outside.  */

  proto::Block blk;

  blk.set_hash (hash);
  blk.set_parent (parent);
  blk.set_height (height);
  blk.set_rngseed (rngseed);
  blk.set_metadata (StoreJson (metadata));

  for (const auto& mv : moves)
    {
      auto& mpb = *blk.add_moves ();
      mpb.set_txid (mv.txid);
      mpb.set_ns (mv.ns);
      mpb.set_name (mv.name);
      mpb.set_mv (mv.mv);
      for (const auto& entry : mv.burns)
        mpb.mutable_burns ()->insert ({entry.first, StoreJson (entry.second)});
      CHECK_EQ (mpb.burns_size (), mv.burns.size ());
      mpb.set_metadata (StoreJson (mv.metadata));
    }

  std::string res;
  blk.SerializeToString (&res);
  return res;
}

void
BlockData::Deserialise (const std::string& data)
{
  proto::Block blk;
  CHECK (blk.ParseFromString (data)) << "Failed to parse Block protocol buffer";

  hash = blk.hash ();
  parent = blk.parent ();
  height = blk.height ();
  rngseed = blk.rngseed ();
  metadata = LoadJson (blk.metadata ());

  moves.clear ();
  for (const auto& mpb : blk.moves ())
    {
      moves.emplace_back ();
      auto& mv = moves.back ();
      mv.txid = mpb.txid ();
      mv.ns = mpb.ns ();
      mv.name = mpb.name ();
      mv.mv = mpb.mv ();
      mv.burns.clear ();
      for (const auto& entry : mpb.burns ())
        mv.burns.emplace (entry.first, LoadJson (entry.second));
      CHECK_EQ (mv.burns.size (), mpb.burns_size ());
      mv.metadata = LoadJson (mpb.metadata ());
    }
}

} // namespace xayax
