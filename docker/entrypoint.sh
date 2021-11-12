#!/bin/sh -e

case $1 in
  core | eth)
    bin="/usr/local/bin/xayax-$1"
    shift
    exec $bin \
      --datadir="/xayax" \
      --port=8000 \
      --zmq_address="tcp://${HOST}:28555" \
      --enable_pruning=1000 \
      "$@"
    ;;

  *)
    echo "Unsupported connector type: '$1'"
    exit 1
esac
