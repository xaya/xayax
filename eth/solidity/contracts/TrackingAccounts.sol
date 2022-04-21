// SPDX-License-Identifier: MIT
// Copyright (C) 2021 The Xaya developers

pragma solidity ^0.8.4;

import "@xaya/eth-account-registry/contracts/XayaAccounts.sol";

/**
 * @dev This contract is an extension of the real XayaAccounts registry.
 * In addition to logging moves it also writes them to storage, and allows
 * other contracts to then read (and reset) those logs.
 *
 * This is not meant to be deployed, but we use this instrumentation with
 * an eth_call state overlay (over the real XayaAccounts contract) to
 * extract moves for pending transactions.
 */
contract TrackingAccounts is XayaAccounts
{

  /**
   * @dev The record of a single move, corresponding to the data needed
   * in Xaya X for it (and basically what is logged as well).  This is
   * designed to match the encoded ABI data of the Move log event (exluding
   * the indexed fields) for simple handling in the code.
   */
  struct MoveData
  {
    string ns;
    string name;
    string move;
    uint256 nonce;
    address mover;
    uint256 amount;
    address receiver;
  }

  /**
   * @dev A record of all moves that have been seen by the overridden
   * move function.  This can be retrieved and reset from an external
   * contract that wants to use the instrumentation.
   */
  MoveData[] internal loggedMoves;

  constructor (IERC20 wchi)
    XayaAccounts (wchi, IXayaPolicy (address (0)))
  {
    /* We invoke the constructor of this contract with eth_call to construct
       the final deployed bytecode, but afterwards we just overlay
       that code onto the real XayaAccounts contract.  This means that we
       need the constructor to properly fill in the immutable variables
       (i.e. the WCHI address), but filling in storage like the policy
       is not important.  */
  }

  /**
   * @dev Sends a move using the parent contract (real XayaAccount) method,
   * and then writes a log about the move to storage so all moves can be
   * retrieved afterwards from a contract call.
   */
  function move (string memory ns, string memory name, string memory mv,
                 uint256 nonce, uint256 amount, address receiver)
      public override returns (uint256)
  {
    uint256 actualNonce = super.move (ns, name, mv, nonce, amount, receiver);
    loggedMoves.push (MoveData (ns, name, mv, actualNonce,
                                msg.sender, amount, receiver));
    return actualNonce;
  }

  /**
   * @dev Clears the record of logged moves.
   */
  function clearMoves () public
  {
    delete loggedMoves;
  }

  /**
   * @dev Returns the current logged moves.
   */
  function getLoggedMoves () public view returns (MoveData[] memory)
  {
    return loggedMoves;
  }

}
