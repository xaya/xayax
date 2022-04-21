// Copyright (C) 2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "config.h"

#include "controller.hpp"

#include "ethchain.hpp"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <cstdlib>
#include <iostream>

namespace
{

DEFINE_string (eth_rpc_url, "",
               "URL for the Ethereum JSON-RPC interface");
DEFINE_string (eth_ws_url, "",
               "URL for the Ethereum websocket endpoint");
DEFINE_string (accounts_contract, "",
               "Address of the Xaya accounts registry contract to use");

DEFINE_string (datadir, "",
               "base data directory for the Xaya X state");

DEFINE_int32 (port, 0,
              "the port where Xaya X should listen for RPC requests");
DEFINE_bool (listen_locally, true,
             "whether or not the RPC server should only bind on localhost");
DEFINE_string (zmq_address, "",
               "the address to bind the ZMQ publisher to");

DEFINE_int32 (max_reorg_depth, 1'000,
              "maximum supported depth of reorgs");

DEFINE_string (watch_for_pending_moves, "",
               "comma-separated list of addresses of contracts that we watch"
               " for pending moves");
DEFINE_bool (sanity_checks, false,
             "whether or not to run slow sanity checks for testing");

/**
 * Parses the comma-separated list of addresses and adds them to the
 * watched contracts in the EthChain instance.
 */
void
AddWatchedContracts (xayax::EthChain& base, std::string lst)
{
  CHECK (!lst.empty ());
  while (true)
    {
      const auto pos = lst.find (',');
      if (pos == std::string::npos)
        {
          base.AddWatchedContract (lst);
          return;
        }
      base.AddWatchedContract (lst.substr (0, pos));
      lst = lst.substr (pos + 1);
    }
}

} // anonymous namespace

int
main (int argc, char* argv[])
{
  google::InitGoogleLogging (argv[0]);

  gflags::SetUsageMessage ("Run Xaya X connector to Ethereum");
  gflags::SetVersionString (PACKAGE_VERSION);
  gflags::ParseCommandLineFlags (&argc, &argv, true);

  try
    {
      if (FLAGS_eth_rpc_url.empty ())
        throw std::runtime_error ("--eth_rpc_url must be set");
      if (FLAGS_accounts_contract.empty ())
        throw std::runtime_error ("--accounts_contract must be set");
      if (FLAGS_port == 0)
        throw std::runtime_error ("--port must be set");
      if (FLAGS_zmq_address.empty ())
        throw std::runtime_error ("--zmq_address must be set");
      if (FLAGS_datadir.empty ())
        throw std::runtime_error ("--datadir must be set");
      if (FLAGS_max_reorg_depth < 0)
        throw std::runtime_error ("--max_reorg_depth must not be negative");

      xayax::EthChain base(FLAGS_eth_rpc_url, FLAGS_eth_ws_url,
                           FLAGS_accounts_contract);
      base.Start ();

      xayax::Controller controller(base, FLAGS_datadir);
      controller.SetMaxReorgDepth (FLAGS_max_reorg_depth);
      controller.SetZmqEndpoint (FLAGS_zmq_address);
      controller.SetRpcBinding (FLAGS_port, FLAGS_listen_locally);
      if (!FLAGS_watch_for_pending_moves.empty ())
        {
          controller.EnablePending ();
          AddWatchedContracts (base, FLAGS_watch_for_pending_moves);
        }
      if (FLAGS_sanity_checks)
        controller.EnableSanityChecks ();

      controller.Run ();
    }
  catch (const std::exception& exc)
    {
      std::cerr << "Error: " << exc.what () << std::endl;
      return EXIT_FAILURE;
    }
  catch (...)
    {
      std::cerr << "Exception caught" << std::endl;
      return EXIT_FAILURE;
    }

  return EXIT_SUCCESS;
}
