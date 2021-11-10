# Copyright (C) 2021 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Test fixture for testing the Xaya-X-on-Ethereum connector.
"""


from xayax import eth, testcase

from xayagametest import premine

from contextlib import contextmanager
import json
import os
import os.path


class Fixture (testcase.BaseChainFixture):

  def addArguments (self, parser):
    parser.add_argument ("--xeth_binary", default="",
                         help="xayax-eth binary to use")

  def getXayaXExtraArgs (self):
    return []

  @contextmanager
  def environment (self):
    with super ().environment ():
      self.w3 = self.env.ganache.w3
      yield

  def createBaseChain (self):
    xethBin = self.args.xeth_binary
    if not xethBin:
      top_builddir = os.getenv ("top_builddir")
      if top_builddir is None:
        top_builddir = "../.."
      xethBin = os.path.join (top_builddir, "eth", "xayax-eth")
    xethCmd = [xethBin] + self.getXayaXExtraArgs ()

    def deploymentCb (env):
      self.setupExtraDeployment (env)

    env = eth.Environment (self.basedir, self.portgen, xethCmd)
    env.addDeploymentCallback (deploymentCb)
    return env.run ()

  def setupExtraDeployment (self, env):
    """
    This method gets invoked when the test environment is started, after
    the basic Ethereum chain is set up but before Xaya X is started.  It can
    be used to perform custom test-specific deployments required.
    """

    pass

  def deployMultiMover (self, env=None):
    """
    Deploys the MultiMover test contract in the environment (defaulting
    to the fixture's).  Returns the deployed contract instance.
    """

    scriptPath = os.path.dirname (os.path.abspath (__file__))
    contracts = os.path.join (scriptPath, "..",
                              "solidity", "build", "contracts")

    with open (os.path.join (contracts, "MultiMover.json")) as f:
      data = json.load (f)

    if env is None:
      env = self.env
    contracts = env.contracts
    deployed = env.ganache.deployContract (contracts.account, data,
                                           contracts.registry.address)

    contracts.registry.functions\
        .setApprovalForAll (deployed.address, True)\
        .transact ({"from": contracts.account})

    return deployed
