// SPDX-License-Identifier: MIT
// Copyright (C) 2021 The Xaya developers

pragma solidity ^0.8.4;

import "@xaya/eth-account-registry/contracts/IXayaAccounts.sol";

/**
 * @dev Test contract that allows sending multiple Xaya moves from a single
 * transaction.  This contract has no access control and should not be used
 * in production!  It is there only for testing purposes.
 */
contract MultiMover
{

  /** @dev The Xaya accounts contract on which we send moves.  */
  IXayaAccounts public immutable accounts;

  constructor (IXayaAccounts acc)
  {
    accounts = acc;

    /* Make sure that the registry contract can spend this contract's WCHI,
       if there is any.  This may be required if the policy has a fee
       for moves (as TestPolicy has for instance).  */
    IERC20 wchi = acc.wchiToken ();
    wchi.approve (address (acc), type (uint256).max);
  }

  /**
   * @dev Sends multiple moves, based on the Cartesian product of the
   * given namespaces, names and move values.
   */
  function send (string[] memory ns, string[] memory names,
                 string[] memory mv) public
  {
    for (uint i = 0; i < ns.length; ++i)
      for (uint j = 0; j < names.length; ++j)
        for (uint k = 0; k < mv.length; ++k)
          accounts.move (ns[i], names[j], mv[k],
                         type (uint256).max, 0, address (0));
  }

  /**
   * @dev Sends a move, but requires an ETH payment to be attached
   * to the transaction.
   */
  function requireEth (string memory ns, string memory name, string memory mv)
      public payable
  {
    require (msg.value >= 10, "not enough ETH paid");
    accounts.move (ns, name, mv, type (uint256).max, 0, address (0));
  }

}
