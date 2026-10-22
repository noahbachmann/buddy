#!/usr/bin/env bash
# Follow the logs of one Buddy service on the Pi.  Usage: deploy/logs.sh pong
set -euo pipefail
HOST="${BUDDY_HOST:-buddy}"
ssh -t "$HOST" journalctl --user-unit "buddy@${1:?service name}" -n 50 -f
