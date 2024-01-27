// Copyright (C) 2023 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockcache.hpp"

#include <glog/logging.h>

namespace xayax
{

/* ************************************************************************** */

void
BlockCacheChain::SetCallbacks (Callbacks* c)
{
  /* We forward the callbacks to the base implementation.  It will then
     call TipChanged/PendingMoves on itself (rather than on this
     instance) if things change, and thus those callbacks will get invoked
     in that way.  */
  base.SetCallbacks (c);
}

void
BlockCacheChain::Start ()
{
  base.Start ();
}

bool
BlockCacheChain::EnablePending ()
{
  return base.EnablePending ();
}

uint64_t
BlockCacheChain::GetTipHeight ()
{
  lastTipHeight = base.GetTipHeight ();
  return lastTipHeight;
}

std::vector<BlockData>
BlockCacheChain::GetBlockRange (const uint64_t start, const uint64_t count)
{
  /* If this range is close to the tip, do not work with the cache at all
     (neither try to query, as the blocks won't be there, nor store).  */
  if (start + count + minDepth > lastTipHeight + 1)
    {
      VLOG (1)
          << "Not using block cache for range "
          << start << "+" << count
          << " close to the tip @" << lastTipHeight;
      return base.GetBlockRange (start, count);
    }

  /* Check if we have all blocks cached.  */
  auto res = store.GetRange (start, count);
  if (res.size () == count)
    {
      VLOG (1) << "All blocks for range " << start << "+" << count << " cached";
      return res;
    }

  /* Otherwise, query the base chain, and save in the cache.  */
  res = base.GetBlockRange (start, count);
  store.Store (res);
  VLOG (1) << "Stored range " << start << "+" << count << " in the cache";

  return res;
}

int64_t
BlockCacheChain::GetMainchainHeight (const std::string& hash)
{
  return base.GetMainchainHeight (hash);
}

std::vector<std::string>
BlockCacheChain::GetMempool ()
{
  return base.GetMempool ();
}

bool
BlockCacheChain::VerifyMessage (const std::string& msg,
                                const std::string& signature,
                                std::string& addr)
{
  return base.VerifyMessage (msg, signature, addr);
}

std::string
BlockCacheChain::GetChain ()
{
  return base.GetChain ();
}

uint64_t
BlockCacheChain::GetVersion ()
{
  return base.GetVersion ();
}

/* ************************************************************************** */

void
InMemoryBlockStorage::Store (const std::vector<BlockData>& blocks)
{
  for (const auto& blk : blocks)
    data.emplace (blk.height, blk);
}

std::vector<BlockData>
InMemoryBlockStorage::GetRange (const uint64_t start, const uint64_t count)
{
  /* Look for the starting height.  If we find it, go from there (as the
     map is ordered) and see if we can get a consecutive range.  */
  auto mit = data.find (start);
  if (mit == data.end ())
    return {};

  std::vector<BlockData> res;
  res.push_back (mit->second);

  while (res.size () < count)
    {
      ++mit;
      if (mit == data.end ())
        return {};
      if (mit->second.height != res.back ().height + 1)
        return {};
      res.push_back (mit->second);
    }

  return res;
}

/* ************************************************************************** */

} // namespace xayax
