// SPDX-License-Identifier: MIT
// Copyright (C) 2024 The Xaya developers

pragma solidity ^0.8.4;

import "@openzeppelin/contracts/token/ERC20/ERC20.sol";

/**
 * @dev Simple test token (representing WCHI) that just mints all the
 * supply to a given address.
 */
contract TestToken is ERC20
{

  constructor (address holder, uint supply)
    ERC20 ("Wrapped CHI", "WCHI")
  {
    _mint (holder, supply);
  }

}
