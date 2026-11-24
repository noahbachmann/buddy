#!/usr/bin/env bash
# Peak RSS and mean CPU of a user unit over a window (host gates, e.g. the screener's in #80).
# The user manager has no memory controller (cgroup.controllers: cpu pids), so MemoryPeak isn't set: this sums the
# VmRSS of every process in the unit's cgroup once a second (children too, e.g. the SDK's CLI per call; shared
# pages count twice: an upper bound).
#   unit-peak.sh <unit> <seconds> [out file]
set -euo pipefail
unit=${1:?unit}
secs=${2:?seconds}
out=${3:-/dev/stdout}

cg=/sys/fs/cgroup$(systemctl --user show "$unit" -p ControlGroup --value)
usage() { awk '$1 == "usage_usec" { print $2 }' "$cg/cpu.stat"; }

start_cpu=$(usage)
start=$(date +%s)
peak=0
while (($(date +%s) - start < secs)); do
    sum=0
    while read -r pid; do
        kb=$(awk '/^VmRSS:/ { print $2 }' "/proc/$pid/status" 2>/dev/null || true)
        sum=$((sum + ${kb:-0}))
    done < "$cg/cgroup.procs"
    ((sum > peak)) && peak=$sum
    sleep 1
done
cpu=$(($(usage) - start_cpu))
elapsed=$(($(date +%s) - start))
{
    echo "$unit $(date -d "@$start" '+%F %T') +${elapsed}s"
    echo "peak RSS: $((peak / 1024)) MB"
    awk -v c="$cpu" -v e="$elapsed" 'BEGIN { printf "mean CPU: %.1f %% of a core\n", c / 1e4 / e }'
    echo "throttled: $(vcgencmd get_throttled 2>/dev/null | cut -d= -f2)"
} >> "$out"
