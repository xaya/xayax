#!/usr/bin/env python3

# Copyright (C) 2021 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Tests signature verification through Xaya X.
"""


import ethtest

from eth_account import Account, messages

import jsonrpclib


def verify (xrpc, addr, msg, sgn):
  return xrpc.verifymessage (address=addr, message=msg, signature=sgn)


if __name__ == "__main__":
  with ethtest.Fixture () as f:
    xrpc = jsonrpclib.ServerProxy (f.env.getXRpcUrl ())

    msg = "Test Message"
    account = Account.create ()
    encoded = messages.encode_defunct (text=msg)
    sgn = account.sign_message (encoded).signature.hex ()

    f.assertEqual (verify (xrpc, "", msg, sgn), {
      "valid": True,
      "address": account.address,
    })
    f.assertEqual (verify (xrpc, "", msg, "invalid"), {
      "valid": False,
    })
    res = verify (xrpc, "", "wrong", sgn)
    f.assertEqual (res["valid"], True)
    assert res["address"] != account.address
