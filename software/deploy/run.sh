#!/usr/bin/env bash
# Run a service's command-line mode on the Pi, with ~/buddy/config/buddy.env loaded like the unit has it. Runs on the PC.
#   deploy/run.sh hue --cmd list
#   deploy/run.sh spotify --cmd devices
#   deploy/run.sh brain --test "play some jazz"
#   deploy/run.sh voice --say "Hello"
# Offline flags only print, but the hardware is real: lights switch, music plays, servos move.
# motion: stop buddy@motion first (it owns /dev/buddy-pico). Paths (--wav) are on the Pi.
#
# Env: BUDDY_HOST (default "buddy", the alias from ~/.ssh/config)
set -euo pipefail
HOST="${BUDDY_HOST:-buddy}"
svc="${1:?service name, e.g. hue}"
shift

# buddy.env may have CRLF (edited on Windows): strip \r
read -r -d '' remote <<'EOF' || true
cr=$(printf '\r')
while IFS="=" read -r k v || [[ -n "$k" ]]; do
    if [[ "$k" =~ ^BUDDY_[A-Z0-9_]+$ ]]; then export "$k=${v%"$cr"}"; fi
done < ~/buddy/config/buddy.env
svc="$1"
shift
exec ~/buddy/bin/buddy-"$svc" "$@"
EOF

tty=()
[[ -t 0 ]] && tty=(-t)  # --login / --pair read from the terminal; no tty when piped
# shellcheck disable=SC2029  # %q-quoted here on purpose
ssh "${tty[@]}" "$HOST" "$(printf '%q ' bash -c "$remote" run "$svc" "$@")"
