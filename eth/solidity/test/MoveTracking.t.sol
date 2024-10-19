// SPDX-License-Identifier: MIT
// Copyright (C) 2021-2024 The Xaya developers

pragma solidity ^0.8.4;

import "./MultiMover.sol";
import "./TestToken.sol";
import "../src/CallForwarder.sol";
import "../src/TrackingAccounts.sol";

import "@xaya/eth-account-registry/contracts/TestPolicy.sol";

import "@openzeppelin/contracts/token/ERC20/IERC20.sol";

import { Test } from "forge-std/Test.sol";

/**
 * @dev Unit tests for call forwarding and tracking moves with the
 * instrumentation contracts.
 */
contract MoveTrackingTest is Test
{

  address public constant operator = address (1);

  IERC20 public wchi;

  TrackingAccounts public xa;
  CallForwarder public fwd;
  MultiMover public mover;

  function setUp () public
  {
    wchi = new TestToken (operator, 78e6 * 1e8);
    IXayaPolicy policy = new TestPolicy ();

    vm.startPrank (operator);
    xa = new TrackingAccounts (wchi);
    xa.schedulePolicyChange (policy);
    vm.warp (xa.policyTimelock () + 1);
    xa.enactPolicyChange ();
    vm.stopPrank ();

    fwd = new CallForwarder (xa);
    mover = new MultiMover (xa);

    /* The TestPolicy imposes a WCHI fee for moves, so we need to make sure that
       the fwd contract has WCHI and the necessary approvals.  */
    vm.startPrank (operator);
    wchi.transfer (address (fwd), 1e8);
    wchi.transfer (address (mover), 1e8);
    vm.startPrank (address (fwd));
    wchi.approve (address (xa), type (uint256).max);
    wchi.approve (address (mover), type (uint256).max);
    xa.setApprovalForAll (address (mover), true);
    vm.stopPrank ();

    /* The test name is owned by the fwd contract, and can thus be moved
       by a forwarded call.  */
    vm.prank (address (fwd));
    xa.register ("p", "test");
  }

  function test_trackDirectMoves () public
  {
    TrackingAccounts.MoveData[] memory res = fwd.execute (address (xa),
        abi.encodeWithSelector (TrackingAccounts.move.selector,
            "p", "test", "x", type (uint256).max, 0, address (0)));
    assertEq (res.length, 1);
    assertEq (res[0].ns, "p");
    assertEq (res[0].name, "test");
    assertEq (res[0].move, "x");
    assertEq (res[0].nonce, 0);
    assertEq (res[0].mover, address (fwd));
    assertEq (res[0].amount, 0);
    assertEq (res[0].receiver, address (0));

    res = fwd.execute (address (xa),
        abi.encodeWithSelector (TrackingAccounts.move.selector,
            "p", "test", "y", type (uint256).max, 0, address (0)));
    assertEq (res.length, 1);
    assertEq (res[0].move, "y");
    assertEq (res[0].nonce, 1);
  }

  function test_revertingMove () public
  {
    vm.expectRevert ();
    fwd.execute (address (xa),
        abi.encodeWithSelector (TrackingAccounts.move.selector,
            "p", "test", "", type (uint256).max, 0, address (0)));
  }

  function test_moveWithChiPayment () public
  {
    address to = address (2);
    TrackingAccounts.MoveData[] memory res = fwd.execute (address (xa),
        abi.encodeWithSelector (TrackingAccounts.move.selector,
            "p", "test", "x", type (uint256).max, 42, to));
    assertEq (res.length, 1);
    assertEq (res[0].amount, 42);
    assertEq (res[0].receiver, to);
    assertEq (wchi.balanceOf (to), 42);
  }

  function test_multipleMoves () public
  {
    string[] memory ns = new string[] (1);
    ns[0] = "p";
    string[] memory name = new string[] (1);
    name[0] = "test";
    string[] memory values = new string[] (2);
    values[0] = "x";
    values[1] = "y";

    TrackingAccounts.MoveData[] memory res = fwd.execute (address (mover),
        abi.encodeWithSelector (MultiMover.send.selector, ns, name, values));
    assertEq (res.length, 2);
    assertEq (res[0].move, "x");
    assertEq (res[0].nonce, 0);
    assertEq (res[1].move, "y");
    assertEq (res[1].nonce, 1);
  }

  function test_ethPayment () public
  {
    bytes memory inner = abi.encodeWithSelector (MultiMover.requireEth.selector,
        "p", "test", "x");
    (bool sent, bytes memory data) = address (fwd).call {value: 20} (
        abi.encodeWithSelector (CallForwarder.execute.selector,
            address (mover), inner));
    assertTrue (sent);

    TrackingAccounts.MoveData[] memory res
        = abi.decode (data, (TrackingAccounts.MoveData[]));
    assertEq (res.length, 1);
    assertEq (res[0].move, "x");
  }

}
