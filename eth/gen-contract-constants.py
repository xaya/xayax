#!/usr/bin/env python3

# Copyright (C) 2021-2024 The Xaya developers
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

def hexWithPrefix (data):
  """
  Converts something to hex() and ensures it has 0x prefix.
  """

  res = data.hex ()
  if res[:2] != "0x":
    res = "0x" + res

  return res

def genSignature (abi, typ, name):
  """
  Generate the signature of a function or event from the ABI JSON.
  """

  [inp] = [
    o["inputs"]
    for o in abi
    if "name" in o and (o["type"], o["name"]) == (typ, name)
  ]

  return name + "(" + ",".join ([
    i["type"] for i in inp
  ]) + ")"

# Load the XayaAccounts ABI to generate signature hashes.
with open (os.path.join ("solidity", "node_modules", "@xaya",
                         "eth-account-registry", "build", "contracts",
                         "XayaAccounts.json")) as f:
  accAbi = json.load (f)["abi"]
  sgn = genSignature (accAbi, "event", "Move")
  print ("const std::string MOVE_EVENT = \""
            + hexWithPrefix (Web3.keccak (sgn.encode ("ascii")))
            + "\";")

# Store the function selectors of select calls we need.
def outputFcnSelector (abi, name, var):
  sgn = genSignature (abi, "function", name)
  print (f"const std::string {var} = \""
            + hexWithPrefix (Web3.keccak (sgn.encode ("ascii"))[:4])
            + "\";")
outputFcnSelector (accAbi, "wchiToken", "ACCOUNT_WCHI_FCN")
with open (os.path.join ("solidity", "build", "contracts",
                         "CallForwarder.json")) as f:
  abi = json.load (f)["abi"]
  outputFcnSelector (abi, "execute", "FORWARDER_EXECUTE_FCN")

# Store the deploying bytecode for our overlay contracts.
def outputContract (name, var):
  with open (os.path.join ("solidity", "build", "contracts",
                           f"{name}.json")) as f:
    data = json.load (f)
    print (f"const std::string {var} = \"{data['bytecode']}\";")
outputContract ("CallForwarder", "CALL_FORWARDER_CODE")
outputContract ("TrackingAccounts", "TRACKING_ACCOUNTS_CODE")

print ("} // namespace xayax")
