#! /bin/sh

case "$1" in
  start)
    echo "Starting server"
    /usr/bin/aesdsocket -d
    ;;
  stop)
    echo "Stopping server"
    pkill aesdsocket
    ;;
  *)
    echo "Usage: $0 {start|stop}"
  exit 1
  esac

  exit 0