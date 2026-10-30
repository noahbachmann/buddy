#!/usr/bin/env bash
# Deploy Buddy to the Pi: sync sources, Release build on the Pi, install, (re)start services.
#
# Usage: deploy/deploy.sh [--setup] [--firmware] [service ...]
#   --setup     run deploy/pi/setup.sh on the Pi first (asks for your sudo password)
#   --firmware  build firmware/pico-motion on the Pi and flash the Pico (D17); alone: no services
#   service     e.g. "ears"; default: all services in SERVICES
#
# Env: BUDDY_HOST (default "buddy", the alias from ~/.ssh/config)
set -euo pipefail

HOST="${BUDDY_HOST:-buddy}"
SERVICES=(ears brain spotify hue web voice motion speakers)
ROOT="$(cd "$(dirname "$0")/.." && pwd)"   # project root = parent of deploy/

setup=false
firmware=false
while [[ "${1:-}" == --* ]]; do
    case "$1" in
        --setup) setup=true ;;
        --firmware) firmware=true ;;
        *) echo "unknown option $1" >&2; exit 2 ;;
    esac
    shift
done
services=("$@")
if [[ ${#services[@]} -eq 0 ]] && ! $firmware; then services=("${SERVICES[@]}"); fi

"$ROOT/deploy/sync.sh"

if $setup; then
    ssh -t "$HOST" 'bash ~/buddy/src/deploy/pi/setup.sh'
fi

if $firmware; then
    echo "→ firmware: build, flash"
    # -f: reboots a running firmware into the bootloader (USB reset interface); a Pico already in BOOTSEL works too.
    # -x: start the new firmware. First configure downloads the SDK + TinyUSB into build-pico.
    ssh "$HOST" bash -s <<'REMOTE'
set -euo pipefail
cd ~/buddy
cmake -S src/firmware/pico-motion -B build-pico -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build build-pico
/opt/picotool/bin/picotool load -x -f build-pico/buddy-motion.uf2
REMOTE
    [[ ${#services[@]} -gt 0 ]] || { echo "✓ firmware flashed. Check: ssh buddy ls -l /dev/buddy-pico"; exit 0; }
fi

echo "→ build, install, restart: ${services[*]}"
ssh "$HOST" bash -s -- "${services[@]}" <<'REMOTE'
set -euo pipefail
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
cd ~/buddy

# C++ services: a CMake target and install component each. Python services (pyproject.toml, D15): a package
# in the uv workspace, installed into ~/buddy/venv; its console script is linked into ~/buddy/bin like a binary.
cpp=()
py=()
units=()
for s in "$@"; do
    if [[ -f "src/services/$s/pyproject.toml" ]]; then py+=("$s"); else cpp+=("$s"); fi
    units+=("buddy@$s")
done
if [[ ${#cpp[@]} -gt 0 ]]; then
    cmake -S src -B build-release -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DBUDDY_TESTS=OFF \
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
        -DCMAKE_INSTALL_PREFIX="$HOME/buddy" > /dev/null
    cmake --build build-release --target "${cpp[@]/#/buddy-}"
fi

# Stop before install: overwriting a running binary fails ("Text file busy"). Install only the services
# being deployed, so other services keep running their current version.
systemctl --user stop "${units[@]}" 2>/dev/null || true
for s in "${cpp[@]}"; do cmake --install build-release --component "$s" > /dev/null; done
for s in "${py[@]}"; do
    # --inexact: keep the other Python services' packages in the shared venv. System Python (Debian), no downloads.
    UV_PROJECT_ENVIRONMENT=~/buddy/venv UV_PYTHON_PREFERENCE=only-system \
        ~/.local/bin/uv sync --project src --frozen --no-dev --inexact --package "buddy-$s" --quiet
    ln -sfn ~/buddy/venv/bin/"buddy-$s" ~/buddy/bin/"buddy-$s"
done
install -m 644 src/deploy/config/systemd/buddy@.service ~/.config/systemd/user/
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
