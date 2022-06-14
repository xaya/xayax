#!/usr/bin/env python3

# Copyright (C) 2021-2022 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Tests message verification through Xaya X.
"""


import coretest

import base64
import codecs
import jsonrpclib


def verify (xrpc, addr, msg, sgn):
  return xrpc.verifymessage (address=addr, message=msg, signature=sgn)


if __name__ == "__main__":
  with coretest.Fixture () as f:
    rpc = f.env.createCoreRpc ()
    xrpc = jsonrpclib.ServerProxy (f.env.getXRpcUrl ())

    msg = "Test Message"
    addr = rpc.getnewaddress ()
    otherAddr = rpc.getnewaddress ()
    sgn = rpc.signmessage (addr, msg)
    invalidSgn = codecs.decode (base64.b64encode (b"invalid"), "ascii")

    f.assertEqual (verify (xrpc, addr, msg, sgn), True)
    f.assertEqual (verify (xrpc, addr, "wrong", sgn), False)
    f.assertEqual (verify (xrpc, addr, msg, invalidSgn), False)
    f.assertEqual (verify (xrpc, otherAddr, msg, sgn), False)
    f.assertEqual (verify (xrpc, "invalid", msg, sgn), False)
    f.assertRaises (verify, xrpc, addr, msg, "not base64")

    f.assertEqual (verify (xrpc, "", msg, sgn), {
      "valid": True,
      "address": addr,
    })
    f.assertEqual (verify (xrpc, "", msg, invalidSgn), {
      "valid": False,
    })
    res = verify (xrpc, "", "wrong", sgn)
    f.assertEqual (res["valid"], True)
    assert res["address"] != addr
