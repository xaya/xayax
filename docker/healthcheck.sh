#!/bin/sh -e

curl -sf \
  -X POST http://localhost:8000 \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","method":"getblockchaininfo","id":1}' \
  | grep -q '"result"'
