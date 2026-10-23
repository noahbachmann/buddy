#!/usr/bin/env bash
# Deploy Buddy to the Pi: sync sources, Release build on the Pi, install, (re)start services.
#
# Usage: deploy/deploy.sh [--setup] [service ...]
#   --setup   run deploy/setup-pi.sh on the Pi first (asks for your sudo password)
#   service   e.g. "ears"; default: all services in SERVICES
#
# Env: BUDDY_HOST (default "buddy", the alias from ~/.ssh/config)
set -euo pipefail

HOST="${BUDDY_HOST:-buddy}"
SERVICES=(ears brain spotify voice mixer)
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
targets=()
units=()
for s in "$@"; do targets+=("buddy-$s"); units+=("buddy@$s"); done
cmake --build build-release --target "${targets[@]}"

# Stop before install: overwriting a running binary fails ("Text file busy"). Install only the services
# being deployed (one CMake component each), so other services keep running their current binary.
systemctl --user stop "${units[@]}" 2>/dev/null || true
for s in "$@"; do cmake --install build-release --component "$s" > /dev/null; done
install -m 644 src/deploy/systemd/buddy@.service src/deploy/systemd/buddy-soloist*.service \
    src/deploy/systemd/buddy-soloist-update.timer ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --quiet "${units[@]}"
# Type=notify: start returns once every service has loaded its models (READY=1), or fails.
systemctl --user start "${units[@]}" || true

failed=0
for u in "${units[@]}"; do
    state="$(systemctl --user is-active "$u" || true)"
    printf '  %-16s %s\n' "$u" "$state"
    [[ "$state" == active ]] || failed=1
done
exit "$failed"
REMOTE
echo "✓ deployed. Logs: deploy/logs.sh <service>, health: deploy/status.sh"
