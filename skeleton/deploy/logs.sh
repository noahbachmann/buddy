#!/usr/bin/env bash
# Follow the logs of one Buddy service on the Pi.
#   deploy/logs.sh ears            → buddy@ears
#   deploy/logs.sh buddy-soloist   → full unit names work too
# --user-unit, not --user: with RAM-only journald (Pi default) there are no per-user journal files.
set -euo pipefail
HOST="${BUDDY_HOST:-buddy}"
unit="${1:?service name}"
[[ "$unit" == buddy-* ]] || unit="buddy@$unit"
ssh -t "$HOST" journalctl --user-unit "$unit" -n 50 -f
