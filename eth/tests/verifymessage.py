#!/usr/bin/env python3

# Copyright (C) 2021-2022 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Tests signature verification through Xaya X.
"""


import ethtest

from eth_account import Account, messages

import base64
import codecs
import jsonrpclib


def verify (xrpc, addr, msg, sgn):
  return xrpc.verifymessage (address=addr, message=msg, signature=sgn)


if __name__ == "__main__":
  with ethtest.Fixture () as f:
    xrpc = jsonrpclib.ServerProxy (f.env.getXRpcUrl ())

    msg = "Test \00Message"
    fullMsg = "Xaya signature for chain %d:\n\n%s" % (f.w3.eth.chainId, msg)
    account = Account.create ()
    encoded = messages.encode_defunct (text=fullMsg)
    rawSgn = account.sign_message (encoded).signature
    sgn = codecs.decode (base64.b64encode (rawSgn), "ascii")

    f.assertEqual (verify (xrpc, "", msg, sgn), {
      "valid": True,
      "address": account.address,
    })
    invalidSgn = codecs.decode (base64.b64encode (b"invalid"), "ascii")
    f.assertEqual (verify (xrpc, "", msg, invalidSgn), {
      "valid": False,
    })
    res = verify (xrpc, "", "wrong", sgn)
    f.assertEqual (res["valid"], True)
    assert res["address"] != account.address

    # Verify also the Environment signing feature (i.e. that it matches
    # up with the expected signing scheme).
    addr = f.env.createSignerAddress ()
    sgn = f.env.signMessage (addr, msg)
    f.assertEqual (verify (xrpc, addr, msg, sgn), True)
