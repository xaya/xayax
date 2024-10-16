// SPDX-License-Identifier: MIT
// Copyright (C) 2024 The Xaya developers

pragma solidity ^0.8.4;

/* This file is here just to force Forge to build the XayaPolicy and NftMetadata
   contracts, too, whose build artefacts we need for the Python testing
   package.  */

import "@xaya/eth-account-registry/contracts/NftMetadata.sol";
import "@xaya/eth-account-registry/contracts/XayaPolicy.sol";
