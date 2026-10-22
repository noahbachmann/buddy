#!/usr/bin/env bash
# Deploy Buddy to the Pi: sync sources, Release build on the Pi, install, (re)start services.
#
# Usage: deploy/deploy.sh [--setup] [service ...]
#   --setup   run deploy/setup-pi.sh on the Pi first (asks for your sudo password)
#   service   e.g. "pong"; default: all services in SERVICES
#
# Env: BUDDY_HOST (default "buddy", the alias from ~/.ssh/config)
set -euo pipefail

HOST="${BUDDY_HOST:-buddy}"
SERVICES=(ping pong)
ROOT="$(cd "$(dirname "$0")/.." && pwd)"   # project root = parent of deploy/

setup=false
if [[ "${1:-}" == "--setup" ]]; then setup=true; shift; fi
services=("$@")
[[ ${#services[@]} -eq 0 ]] && services=("${SERVICES[@]}")

if [[ -n "$(git -C "$ROOT" status --porcelain)" ]]; then
    echo "⚠  deploying uncommitted changes" >&2
fi

echo "→ sync → $HOST:~/buddy/src"
ssh "$HOST" 'mkdir -p ~/buddy/src'
rsync -az --delete --progress \
    --exclude '.git/' --exclude '.idea/' --exclude 'cmake-build-*/' --exclude '/build*/' \
    "$ROOT/" "$HOST:buddy/src/"

if $setup; then
    ssh -t "$HOST" 'bash ~/buddy/src/deploy/setup-pi.sh'
fi

echo "→ build, install, restart: ${services[*]}"
ssh "$HOST" bash -s -- "${services[@]}" <<'REMOTE'
set -euo pipefail
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
cd ~/buddy

cmake -S src -B build-release -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_INSTALL_PREFIX="$HOME/buddy" > /dev/null
cmake --build build-release

units=()
for s in "$@"; do units+=("buddy@$s"); done

# Stop before install: overwriting a running binary fails ("Text file busy").
systemctl --user stop "${units[@]}" 2>/dev/null || true
cmake --install build-release > /dev/null
install -m 644 src/deploy/systemd/buddy@.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --quiet "${units[@]}"
systemctl --user start "${units[@]}"

sleep 1
for u in "${units[@]}"; do
    printf '  %-16s %s\n' "$u" "$(systemctl --user is-active "$u" || true)"
done
REMOTE
echo "✓ deployed. Logs: deploy/logs.sh <service>"
