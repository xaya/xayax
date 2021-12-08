// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "testutils.hpp"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <chrono>
#include <sstream>

namespace xayax
{

Json::Value
ParseJson (const std::string& str)
{
  std::istringstream in(str);
  Json::Value res;
  in >> res;
  return res;
}

void
SleepSome ()
{
  std::this_thread::sleep_for (std::chrono::milliseconds (10));
}

/* ************************************************************************** */

TestBaseChain::TestBaseChain ()
  : chain(":memory:")
{}

TestBaseChain::~TestBaseChain ()
{
  mut.lock ();
  if (notifier != nullptr)
    {
      shouldStop = true;
      cvNewTip.notify_all ();
      mut.unlock ();
      notifier->join ();
      mut.lock ();
      notifier.reset ();
    }
  mut.unlock ();
}

std::string
TestBaseChain::NewBlockHash ()
{
  std::lock_guard<std::mutex> lock(mut);

  ++hashCounter;

  std::ostringstream res;
  res << "block " << hashCounter;

  return res.str ();
}

BlockData
TestBaseChain::NewGenesis (const uint64_t h)
{
  BlockData res;
  res.hash = NewBlockHash ();
  res.parent = "pregenesis";
  res.height = h;
  return res;
}

BlockData
TestBaseChain::NewBlock (const std::string& parent)
{
  BlockData res;
  res.hash = NewBlockHash ();
  res.parent = parent;

  std::lock_guard<std::mutex> lock(mut);
  res.height = blocks.at (res.parent).height + 1;

  return res;
}

BlockData
TestBaseChain::NewBlock ()
{
  std::string parent;
  {
    std::lock_guard<std::mutex> lock(mut);
    const uint64_t tipHeight = chain.GetTipHeight ();
    CHECK_GE (tipHeight, 0);
    CHECK (chain.GetHashForHeight (tipHeight, parent));
  }

  return NewBlock (parent);
}

BlockData
TestBaseChain::SetGenesis (const BlockData& blk)
{
  std::lock_guard<std::mutex> lock(mut);

  blocks[blk.hash] = blk;
  chain.Initialise (blk);

  cvNewTip.notify_all ();

  return blk;
}

BlockData
TestBaseChain::SetTip (const BlockData& blk)
{
  std::lock_guard<std::mutex> lock(mut);

  blocks[blk.hash] = blk;
  std::string oldTip;
  CHECK (chain.SetTip (blk, oldTip));

  cvNewTip.notify_all ();

  return blk;
}

std::vector<BlockData>
TestBaseChain::AttachBranch (const std::string& parent, const unsigned n)
{
  std::vector<BlockData> res;
  for (unsigned i = 0; i < n; ++i)
    {
      BlockData blk;
      if (i == 0)
        blk = NewBlock (parent);
      else
        blk = NewBlock ();
      SetTip (blk);
      res.push_back (std::move (blk));
    }
  return res;
}

void
TestBaseChain::AddPending (const std::vector<MoveData>& moves)
{
  std::lock_guard<std::mutex> lock(mut);
  CHECK (!moves.empty ());
  mempool.push_back (moves.front ().txid);
  PendingMoves (moves);
}

void
TestBaseChain::SetChain (const std::string& str)
{
  std::lock_guard<std::mutex> lock(mut);
  chainString = str;
}

void
TestBaseChain::SetVersion (const uint64_t v)
{
  std::lock_guard<std::mutex> lock(mut);
  version = v;
}

void
TestBaseChain::Start ()
{
  std::lock_guard<std::mutex> lock(mut);
  CHECK (notifier == nullptr);

  shouldStop = false;
  notifier = std::make_unique<std::thread> ([this] ()
    {
      std::unique_lock<std::mutex> lock(mut);
      while (!shouldStop)
        {
          cvNewTip.wait (lock);
          /* Avoid a spurious callback when the thread is woken up not
             because of a notification but because we are shutting down.  */
          if (shouldStop)
            continue;

          const uint64_t newHeight = chain.GetTipHeight ();
          std::string newTip;
          CHECK (chain.GetHashForHeight (newHeight, newTip));

          /* TipChanged may call back into GetBlockRange (through Sync),
             so make sure that won't deadlock.  */
          lock.unlock ();
          TipChanged (newTip);
          lock.lock ();
        }
    });
}

bool
TestBaseChain::EnablePending ()
{
  /* Nothing specific, just signal that we support pending.  */
  return true;
}

std::vector<BlockData>
TestBaseChain::GetBlockRange (const uint64_t start, const uint64_t count)
{
  std::lock_guard<std::mutex> lock(mut);
  std::vector<BlockData> res;

  for (uint64_t h = start; h < start + count; ++h)
    {
      std::string hash;
      if (!chain.GetHashForHeight (h, hash))
        break;

      res.push_back (blocks.at (hash));
    }

  return res;
}

int64_t
TestBaseChain::GetMainchainHeight (const std::string& hash)
{
  std::lock_guard<std::mutex> lock(mut);

  uint64_t height;
  if (!chain.GetHeightForHash (hash, height))
    return -1;

  /* The block is known, but we need to verify as well if it is on
     the main chain.  */
  std::string mainchainHash;
  if (!chain.GetHashForHeight (height, mainchainHash))
    return -1;
  if (mainchainHash != hash)
    return -1;

  return height;
}

std::vector<std::string>
TestBaseChain::GetMempool ()
{
  std::lock_guard<std::mutex> lock(mut);
  return mempool;
}

bool
TestBaseChain::VerifyMessage (const std::string& msg,
                              const std::string& signature, std::string& addr)
{
  LOG (FATAL) << "TestBaseChain::VerifyMessage is not implemented";
}

std::string
TestBaseChain::GetChain ()
{
  std::lock_guard<std::mutex> lock(mut);
  return chainString;
}

uint64_t
TestBaseChain::GetVersion ()
{
  std::lock_guard<std::mutex> lock(mut);
  return version;
}

/* ************************************************************************** */

TestZmqSubscriber::TestZmqSubscriber (const std::string& addr)
  : sock(ctx, ZMQ_SUB)
{
  std::lock_guard<std::mutex> lock(mut);

  sock.connect (addr);
  sock.set (zmq::sockopt::subscribe, "");
  LOG (INFO) << "Connected ZMQ subscriber to " << addr;

  shouldStop = false;
  receiver = std::make_unique<std::thread> ([this] ()
    {
      ReceiveLoop ();
    });
}

TestZmqSubscriber::~TestZmqSubscriber ()
{
  std::unique_lock<std::mutex> lock(mut);

  shouldStop = true;
  lock.unlock ();
  receiver->join ();
  lock.lock ();

  receiver.reset ();
  sock.close ();

  for (const auto& m : messages)
    EXPECT_TRUE (m.second.empty ())
        << "Unexpected messages for " << m.first << " received";
}

void
TestZmqSubscriber::ReceiveLoop ()
{
  std::unique_lock<std::mutex> lock(mut);
  while (!shouldStop)
    {
      zmq::message_t msg;
      if (!sock.recv (msg, zmq::recv_flags::dontwait))
        {
          /* No messages available.  Just sleep a bit and try again.  */
          lock.unlock ();
          std::this_thread::sleep_for (std::chrono::milliseconds (1));
          lock.lock ();
          continue;
        }

      const std::string topic = msg.to_string ();
      VLOG (1) << "Received notification: " << topic;
      ASSERT_EQ (sock.get (zmq::sockopt::rcvmore), 1);

      /* Messages are delivered atomically, so the other parts must be here.  */
      ASSERT_TRUE (sock.recv (msg, zmq::recv_flags::dontwait));
      const std::string data = msg.to_string ();
      ASSERT_EQ (sock.get (zmq::sockopt::rcvmore), 1);

      ASSERT_TRUE (sock.recv (msg, zmq::recv_flags::dontwait));
      const uint8_t* seqBytes = static_cast<const uint8_t*> (msg.data ());
      uint32_t seq = 0;
      ASSERT_EQ (msg.size (), sizeof (seq)) << "Invalid sized sequence number";
      ASSERT_EQ (sock.get (zmq::sockopt::rcvmore), 0);
      for (unsigned i = 0; i < sizeof (seq); ++i)
        seq |= seqBytes[i] << (8 * i);

      /* Check that the sequence number matches.  */
      ASSERT_EQ (seq, nextSeq[topic]);
      ++nextSeq[topic];

      /* Parse and enqueue the message.  */
      messages[topic].push (ParseJson (data));
      cv.notify_all ();
    }
}

std::vector<Json::Value>
TestZmqSubscriber::AwaitMessages (const std::string& cmd, const size_t num)
{
  std::unique_lock<std::mutex> lock(mut);

  std::vector<Json::Value> res;
  while (res.size () < num)
    {
      while (messages[cmd].empty ())
        cv.wait (lock);

      auto& received = messages[cmd];
      res.push_back (std::move (received.front ()));
      received.pop ();
    }

  return res;
}

/* ************************************************************************** */

} // namespace xayax
