#!/usr/bin/env bash
# Follow the logs of one Buddy service on the Pi. Runs on the PC.
#   deploy/logs.sh ears          → buddy@ears
#   deploy/logs.sh pipewire      → any other user unit by its full name
# --user-unit, not --user: with RAM-only journald (Pi default) there are no per-user journal files.
set -euo pipefail
HOST="${BUDDY_HOST:-buddy}"
unit="${1:?service name}"
[[ " ears brain spotify voice " == *" $unit "* ]] && unit="buddy@$unit"
ssh -t "$HOST" journalctl --user-unit "$unit" -n 50 -f
