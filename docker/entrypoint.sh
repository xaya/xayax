#!/bin/sh -e

# If no HOST is explicitly set, try to detect it automatically.
if [[ -z $HOST ]]
then
  export HOST=$(hostname -i)
  echo "Using detected host: ${HOST}"
fi

case $1 in
  core | eth)
    bin="/usr/local/bin/xayax-$1"
    shift
    exec $bin \
      --datadir="/xayax" \
      --port=8000 \
      --zmq_address="tcp://${HOST}:28555" \
      --max_reorg_depth="${MAX_REORG_DEPTH}" \
      "$@"
    ;;

  *)
    echo "Unsupported connector type: '$1'"
    exit 1
esac
