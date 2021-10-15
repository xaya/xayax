// SPDX-License-Identifier: MIT
// Copyright (C) 2021 Autonomous Worlds Ltd

const truffleAssert = require ("truffle-assertions");
const truffleContract = require ("truffle-contract");
const { time } = require ("@openzeppelin/test-helpers");

/* We want to use chai-subset for checking the MoveData structs easily,
   but due to an open issue with Truffle we can only apply the plugin
   if we use our own Chai (for these assertions at least):

     https://github.com/trufflesuite/truffle/issues/2090
*/
const chai = require ("chai");
const chaiSubset = require ("chai-subset");
chai.use (chaiSubset);

const loadXayaContract = (pkg, name) => {
  const path = "@xaya/" + pkg + "/build/contracts/" + name + ".json";
  const data = require (path);
  const res = truffleContract (data);
  res.setProvider (web3.currentProvider);
  return res;
};

const WCHI = loadXayaContract ("wchi", "WCHI");
const TestPolicy = loadXayaContract ("eth-account-registry", "TestPolicy");

const TrackingAccounts = artifacts.require ("TrackingAccounts");
const CallForwarder = artifacts.require ("CallForwarder");
const MultiMover = artifacts.require ("MultiMover");

const zeroAddr = "0x0000000000000000000000000000000000000000";
const maxUint256 = "115792089237316195423570985008687907853269984665640564039457584007913129639935";
const bnMaxUint256 = web3.utils.toBN (maxUint256);
const noNonce = bnMaxUint256;

/*** ************************************************************************ */

contract ("CallForwarder", accounts => {
  const operator = accounts[0];
  const op = {"from": operator};

  let wchi, policy;
  before (async () => {
    wchi = await WCHI.new (op);
    policy = await TestPolicy.new (op);
  });

  let xa, fwd, mover;
  beforeEach (async () => {
    xa = await TrackingAccounts.new (wchi.address, op);
    await wchi.approve (xa.address, bnMaxUint256, op);

    await xa.schedulePolicyChange (policy.address, op);
    time.increase ((await xa.policyTimelock ()) + 1);
    await xa.enactPolicyChange (op);

    fwd = await CallForwarder.new (xa.address, op);
    mover = await MultiMover.new (xa.address, op);

    /* The test name is owned by the fwd contract, and can thus be moved
       by a forwarded call.  */
    await xa.register ("p", "test", op);
    const token = await xa.tokenIdForName ("p", "test");
    await xa.transferFrom (operator, fwd.address, token, op);

    /* The TestPolicy imposes a WCHI fee for moves, so we need to make sure
       that the fwd contract has WCHI and the necessary approvals.  */
    await wchi.transfer (fwd.address, 100, op);
    await wchi.transfer (mover.address, 100, op);
    const execFwd = (to, call) => {
      const data = call.encodeABI ();
      return fwd.execute (to, data, op);
    };
    await execFwd (wchi.address,
                   wchi.contract.methods.approve (xa.address, 100));
    await execFwd (wchi.address,
                   wchi.contract.methods.approve (mover.address, 100));
    await execFwd (xa.address,
                   xa.contract.methods.setApprovalForAll (mover.address, true));
  });

  /* Helper method that performs a (simulated) call through the
     forwarder contract.  to is the target address and call the
     web3 contract method instance.  */
  const forward = (to, call) => {
    const data = call.encodeABI ();
    return fwd.execute.call (to, data);
  };

  /* ************************************************************************ */

  it ("should track direct moves", async () => {
    let res;
    res = await forward (xa.address,
                         xa.contract.methods.move (
                            "p", "test", "x", noNonce, 0, zeroAddr));
    assert.lengthOf (res, 1);
    chai.expect (res[0]).to.containSubset ({
      "ns": "p",
      "name": "test",
      "move": "x",
      "nonce": "0",
      "mover": fwd.address,
      "amount": "0",
      "receiver": zeroAddr,
    });

    res = await forward (xa.address,
                         xa.contract.methods.move (
                            "p", "test", "y", noNonce, 0, zeroAddr));
    assert.lengthOf (res, 1);
    chai.expect (res[0]).to.containSubset ({
      "move": "y",
    });
  });

  it ("should handle moves that revert", async () => {
    await truffleAssert.reverts (
        forward (xa.address,
                 xa.contract.methods.move (
                    "p", "test", "", noNonce, 0, zeroAddr)));
  });

  it ("should track moves with CHI payment", async () => {
    const to = accounts[1];
    const res = await forward (xa.address,
                               xa.contract.methods.move (
                                  "p", "test", "x", noNonce, 42, to));
    assert.lengthOf (res, 1);
    chai.expect (res[0]).to.containSubset ({
      "amount": "42",
      "receiver": to,
    });
  });

  it ("should handle multiple moves in a transaction", async () => {
    const res = await forward (mover.address,
                               mover.contract.methods.send (
                                  ["p"], ["test"], ["x", "y"]));
    assert.lengthOf (res, 2);
    chai.expect (res[0]).to.containSubset ({
      "move": "x",
      "nonce": "0",
    });
    chai.expect (res[1]).to.containSubset ({
      "move": "y",
      "nonce": "1",
    });
  });

  it ("should handle ETH payments with forwarded calls", async () => {
    const call = mover.contract.methods.requireEth ("p", "test", "x");
    const data = call.encodeABI ();

    const res = await fwd.execute.call (mover.address, data, {"value": 20});
    assert.lengthOf (res, 1);
    chai.expect (res[0]).to.containSubset ({
      "move": "x",
    });
  });

  /* ************************************************************************ */

});
