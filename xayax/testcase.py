# Copyright (C) 2021 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Generic test framework for Python integration tests of Xaya X connectors.
"""

import jsonrpclib
import zmq

import argparse
import codecs
from contextlib import contextmanager
import json
import logging
import os
import os.path
import random
import shutil
import struct
import sys


DEFAULT_DIR = "/tmp"
DIR_PREFIX = "xayaxtest_"


class ZmqSubscriber:
  """
  Helper class for listening to the ZMQ notifications sent by a Xaya X
  process under test.
  """

  def __init__ (self, ctx, rpcUrl, game):
    self.sequence = {}
    self.game = game
    self.prefixes = set ()
    self.ctx = ctx

    rpc = jsonrpclib.ServerProxy (rpcUrl)
    rpc._notify.trackedgames (command="add", gameid=game)

    info = rpc.getzmqnotifications ()
    self.addr = None
    for notification in info:
      if self.addr is None:
        self.addr = notification["address"]
      else:
        assert self.addr == notification["address"]

  def subscribe (self, prefix):
    """
    Sets a prefix to which we want to subscribe when running.
    """

    self.prefixes.add (prefix)

  @contextmanager
  def run (self):
    """
    Starts to listen on the detected address and with the configured
    prefixes to subscribe to.  When the context finishes, the socket
    is closed / cleaned up, and we assert that no unexpected messages
    have been received.
    """

    self.socket = self.ctx.socket (zmq.SUB)
    self.socket.set (zmq.RCVTIMEO, 60000)
    self.socket.set (zmq.LINGER, 0)
    self.socket.connect (self.addr)

    for p in self.prefixes:
      topic = "%s json %s" % (p, self.game)
      self.socket.setsockopt_string (zmq.SUBSCRIBE, topic)

    yield self

    self.assertNoMessage ()
    self.socket.close ()

  def receive (self):
    """
    Receives the next message from the socket.  Returns the prefix
    (without "json GAMEID") and the payload data as JSON.
    """

    topic, body, seq = self.socket.recv_multipart ()

    topic = codecs.decode (topic, "ascii")
    parts = topic.split (" ")
    assert len (parts) == 3
    assert parts[1] == "json"
    assert parts[2] == self.game
    assert parts[0] in self.prefixes

    # Sequence should be incremental for the full topic string.
    # We may have missed some at the very beginning.
    seqNum = struct.unpack ("<I", seq)[-1]
    if topic in self.sequence:
      assert seqNum == self.sequence[topic]
    else:
      self.sequence[topic] = seqNum
    self.sequence[topic] += 1

    return parts[0], json.loads (codecs.decode (body, "ascii"))

  def assertNoMessage (self):
    try:
      _ = self.socket.recv (zmq.NOBLOCK)
      raise AssertionError ("expected no more messages, but got one")
    except zmq.error.Again:
      pass


class Fixture:
  """
  Generic test fixture which sets up a temporary data directory,
  port generator and handles cleanup of the data files on disk
  (if the test was successful).

  The test fixture works as a context manager.  Upon entering, all the
  setup is done.  Then within the context, the actual test logic
  can be executed, and on leaving, cleanup happens.  If an exception
  is thrown inside, the test is marked as failure.
  """

  def __init__ (self):
    desc = "Runs an integration test for Xaya X"
    parser = argparse.ArgumentParser (description=desc)
    parser.add_argument ("--dir", default=DEFAULT_DIR,
                         help="base directory for test runs")
    parser.add_argument ("--nocleanup", default=False, action="store_true",
                         help="do not clean up logs after success")
    self.addArguments (parser)
    self.args = parser.parse_args ()

    self.ctx = None

  def __enter__ (self):
    randomSuffix = "%08x" % random.getrandbits (32)
    self.basedir = os.path.join (self.args.dir, DIR_PREFIX + randomSuffix)
    shutil.rmtree (self.basedir, ignore_errors=True)
    os.mkdir (self.basedir)

    logfile = os.path.join (self.basedir, "test.log")
    logHandler = logging.FileHandler (logfile)
    logFmt = "%(asctime)s %(name)s (%(levelname)s): %(message)s"
    logHandler.setFormatter (logging.Formatter (logFmt))

    rootLogger = logging.getLogger ()
    rootLogger.setLevel (logging.INFO)
    rootLogger.addHandler (logHandler)

    self.log = logging.getLogger ("xayax.testcase")

    mainHandler = logging.StreamHandler (sys.stderr)
    mainHandler.setFormatter (logging.Formatter ("%(message)s"))

    self.mainLogger = logging.getLogger ("main")
    self.mainLogger.addHandler (logHandler)
    self.mainLogger.addHandler (mainHandler)
    self.mainLogger.info ("Base directory for integration test: %s"
                            % self.basedir)

    startPort = random.randint (1024, 30000)
    self.log.info ("Using port range starting at %d, hopefully it is free"
        % (startPort))

    def portGenerator (start):
      p = start
      while True:
        yield p
        p += 1

    self.portgen = portGenerator (startPort)
    self.zmqCtx = zmq.Context ()

    # Create and store the context manager from environment.
    self.subctx = self.environment ()
    self.subctx.__enter__ ()

    return self

  def __exit__ (self, exc, val, tb):
    if self.subctx.__exit__ (exc, val, tb):
      exc = None

    self.zmqCtx.destroy (linger=None)

    if exc:
      self.mainLogger.exception ("Test failed")
      self.log.exception ("Test failed", exc_info=(exc, val, tb))
      cleanup = False
    else:
      self.mainLogger.info ("Test succeeded")
      if self.args.nocleanup:
        self.mainLogger.info ("Not cleaning up logs as requested")
        cleanup = False
      else:
        cleanup = True

    logging.shutdown ()

    if cleanup:
      shutil.rmtree (self.basedir, ignore_errors=True)

    if exc:
      sys.exit ("Test failed")

  def addArguments (self, parser):
    """
    Subclasses can add their own arguments to the argparser here.
    Parsed arguments are accessible in self.args afterwards.
    """

    pass

  @contextmanager
  def environment (self):
    """
    Subclasses can override this method to provide their own context
    manager.  It will be run as part of the fixture itself.
    """

    yield

  def assertEqual (self, a, b):
    """
    Asserts that two values are equal, logging them if not.
    """

    if a == b:
      return

    self.log.error ("The value of:\n%s\n\nis not equal to:\n%s" % (a, b))
    raise AssertionError ("%s != %s" % (a, b))