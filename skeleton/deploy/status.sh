#!/usr/bin/env bash
# Health overview of Buddy on the Pi: services, restarts, memory (RSS), MQTT status, temperature. Runs on the PC.
#   deploy/status.sh        everything
#   deploy/status.sh -l     plus the last 5 log lines per service
# Env: BUDDY_HOST (default "buddy", the alias from ~/.ssh/config)
set -euo pipefail
HOST="${BUDDY_HOST:-buddy}"
LINES=0
[[ "${1:-}" == "-l" ]] && LINES=5

ssh "$HOST" bash -s -- "$LINES" <<'REMOTE'
set -uo pipefail
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
lines="$1"

echo "→ $(hostname), up $(uptime -p | cut -d' ' -f2-), $(vcgencmd measure_temp | cut -d= -f2)"
# 0x0 = fine. Bit 0: undervoltage now, bit 2: throttled now, bit 16/18: happened since boot (power supply!)
echo "  throttled: $(vcgencmd get_throttled | cut -d= -f2)   load: $(cut -d' ' -f1-3 /proc/loadavg)   mosquitto: $(systemctl is-active mosquitto)"
echo

units=$(systemctl --user list-units 'buddy*' --all --plain --no-legend --type=service | awk '{print $1}')
printf '  %-28s %-9s %-8s %8s  %s\n' UNIT STATE RESTARTS MEMORY STATUS
for u in $units; do
    eval "$(systemctl --user show "$u" -p ActiveState -p NRestarts -p MainPID -p StatusText \
        | sed -E "s/^([A-Za-z]+)=(.*)$/\1='\2'/")"
    mem=-
    [[ "$MainPID" != 0 ]] && mem="$(awk '/^VmRSS/ {print int($2 / 1024) " MB"}' "/proc/$MainPID/status" 2>/dev/null)"
    printf '  %-28s %-9s %-8s %8s  %s\n' "$u" "$ActiveState" "$NRestarts" "$mem" "$StatusText"
done

echo
echo "  MQTT status (retained):"
timeout 4 mosquitto_sub -v -t 'buddy/+/status' -W 2 2>/dev/null | sed 's/^/    /' || true

if [[ "$lines" -gt 0 ]]; then
    for u in $units; do
        echo
        echo "── $u"
        journalctl --user-unit "$u" -n "$lines" --no-pager -o cat 2>/dev/null | sed 's/^/  /'
    done
fi
REMOTE
