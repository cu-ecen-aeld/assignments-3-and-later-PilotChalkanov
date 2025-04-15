#! /bin/sh

case "$1" in
  start)
    echo "Starting server"
    start-stop-daemon -S -n aesdsocket -d /usr/bin/aesdsocket
    ;;
  stop)
    echo "Stopping server"
    start-stop-daemon -K -n aesdsocket -s SIGTERM
    ;;
  *)
    echo "Usage: $0 {start|stop}"
  exit 1
  esac

  exit 0