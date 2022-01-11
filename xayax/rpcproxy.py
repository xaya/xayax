# Copyright (C) 2021 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Simple proxy server for JSON-RPC, which just forwards requests to
another host/port.  It has the ability to disconnect and reconnect
on demand, and to toggle on returning errors.  This allows easily
testing situations where e.g. the basechain of Xaya X throws intermittend
failures.
"""

import requests

import contextlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import logging
import threading


class RequestHandler (BaseHTTPRequestHandler):
  """
  Request handler that just forwards POST requests to another host/port,
  or returns a fixed error.
  """

  def do_POST (self):
    request = self.rfile.read (int (self.headers["Content-Length"]))
    data = json.loads (request)

    with self.server.lock:
      errors = self.server.returnErrors

    if errors == "http":
      self.send_error (500, explain="Faked HTTP error")
      return

    if errors == "rpc":
      result = {
        "jsonrpc": "2.0",
        "id": data["id"],
        "error": {
          "code": -32_000,
          "message": "Faked RPC error",
        },
      }
    else:
      assert errors is None, "unexpected error request: %s" % errors
      resp = requests.post (self.server.targetUrl, json=data)
      if resp.status_code != 200:
        self.send_error (resp.status_code)
        return
      result = resp.json ()

    body = json.dumps (result).encode ("ascii")
    self.send_response (200)
    self.send_header ("Content-Length", str (len (body)))
    self.send_header ("Content-Type", "application/json")
    self.end_headers ()
    self.wfile.write (body)

  def log_message (self, fmt, *args):
    pass


class Proxy:
  """
  The main proxy class, which can be run as a server and then forwards
  requests.  It can be stopped/started at will and instructed to return
  faked errors for testing.
  """

  def __init__ (self, addr, targetUrl):
    self.log = logging.getLogger ("xayax.rpcproxy")
    self.address = addr
    self.targetUrl = targetUrl
    self.log.info ("RPC proxy is listening at %s:%d and calls %s"
                      % (addr[0], addr[1], self.targetUrl))

    # This class does not inherit from the HTTPServer class directly, but
    # instead uses an internal instance.  This allows us to properly start
    # and stop the server on demand.
    self.server = None
    self.runner = None

  def start (self):
    """
    Starts the server in a background thread.
    """

    assert not self.server, "server is already running"
    self.log.info ("Starting RPC proxy server")

    self.server = ThreadingHTTPServer (self.address, RequestHandler)
    self.server.targetUrl = self.targetUrl
    self.server.returnErrors = None
    self.server.lock = threading.Lock ()

    self.runner = threading.Thread (target=self.server.serve_forever)
    self.runner.start ()

  def stop (self):
    """
    Stops the running server.
    """

    assert self.server, "server is not running"
    self.server.shutdown ()
    self.runner.join ()
    self.runner = None
    self.server.server_close ()
    self.server = None
    self.log.info ("Stopped RPC proxy server")

  def setErrors (self, typ):
    assert self.server, "server is not running"
    with self.server.lock:
      self.server.returnErrors = typ

  def enableRpcErrors (self):
    """
    Makes the server return RPC errors to requests.
    """

    self.log.info ("Turning on RPC errors in the proxy server")
    self.setErrors ("rpc")

  def enableHttpErrors (self):
    """
    Makes the server return HTTP errors to requests.
    """

    self.log.info ("Turning on HTTP errors in the proxy server")
    self.setErrors ("http")

  def fixup (self):
    """
    Makes the server forward requests normally again.  This starts it if it
    has been stopped, and turns off error returning.
    """

    self.log.info ("Fixing up proxy server")

    if not self.server:
      self.start ()

    self.setErrors (None)

  @contextlib.contextmanager
  def run (self):
    """
    Runs the server normally.
    """

    self.start ()
    try:
      yield self
    finally:
      if self.server:
        self.stop ()
