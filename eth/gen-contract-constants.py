#!/usr/bin/env python3

# Copyright (C) 2021 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
This script reads the compiled Solidity contracts and uses them
together with Web3.py to generate some constants (like event and
method signatures and the overlay contract bytecodes) as C++ code.
"""


from web3 import Web3

import json
import os.path


print ("#include \"contract-constants.hpp\"")
print ("namespace xayax {")

# Load the XayaAccounts ABI to generate signature hashes.
with open (os.path.join ("solidity", "node_modules", "@xaya",
                         "eth-account-registry", "build", "contracts",
                         "XayaAccounts.json")) as f:
  abi = json.load (f)["abi"]

  [mvInputs] = [
    o["inputs"]
    for o in abi
    if "name" in o and (o["type"], o["name"]) == ("event", "Move")
  ]
  sgn = "Move(" + ",".join ([
    i["type"] for i in mvInputs
  ]) + ")"
  print ("const std::string MOVE_EVENT = \""
            + Web3.keccak (sgn.encode ("ascii")).hex ()
            + "\";")

print ("} // namespace xayax")
