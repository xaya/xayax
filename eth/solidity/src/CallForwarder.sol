// SPDX-License-Identifier: MIT
// Copyright (C) 2021 The Xaya developers

pragma solidity ^0.8.4;

import "./TrackingAccounts.sol";

import "@openzeppelin/contracts/token/ERC721/utils/ERC721Holder.sol";

/**
 * @dev This contract can forward arbitrary calls, while using the
 * TrackingAccounts instrumentation of the Xaya registry and then returning
 * a record of moves that were logged.
 *
 * The contract is not meant to be deployed, but will be used in a state
 * overlay with eth_call to impersonate the EOA sending a transaction
 * for the instrumentation.
 *
 * This contract implements the ERC721Receiver interface so that it can
 * properly receive tokens (in particular, Xaya account names on registration),
 * which is something EOA addresses would be able to do as well.
 */
contract CallForwarder is ERC721Holder
{

  /**
   * @dev The TrackingAccounts instance we use (in other words, the
   * address of the XayaAccounts registry deployed on chain).  As an
   * immutable variable, this will be filled into the deployed bytecode
   * itself during contract creation (which we simulate first), and thus
   * works with the state overlay independent of the contract state.
   */
  TrackingAccounts internal immutable accounts;

  constructor (TrackingAccounts acc)
  {
    accounts = acc;
  }

  /**
   * @dev Forwards a call with the given raw data to the receiver,
   * including any amount of ETH that might be paid.  This uses
   * the instrumentation in the accounts registry to return the
   * record of logged moves from the call.
   */
  function execute (address to, bytes calldata data)
      external payable returns (TrackingAccounts.MoveData[] memory)
  {
    accounts.clearMoves ();
    (bool ok, bytes memory rd) = to.call {value: msg.value} (data);
    require (ok, string (rd));
    return accounts.getLoggedMoves ();
  }

}
