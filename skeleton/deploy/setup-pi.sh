#!/usr/bin/env bash
# One-time Pi setup for Buddy. Safe to run again (idempotent).
# Runs ON the Pi; deploy.sh --setup calls it for you.
set -euo pipefail
cd "$(dirname "$0")"

echo "→ packages"
sudo apt-get update
sudo apt-get install -y \
    build-essential cmake ninja-build gdb rsync git ccache pkg-config \
    mosquitto mosquitto-clients \
    libpaho-mqttpp-dev nlohmann-json3-dev

echo "→ mosquitto (localhost only for now)"
sudo systemctl enable --now mosquitto

echo "→ lingering: user services start at boot and survive SSH logout"
sudo loginctl enable-linger "$USER"

echo "→ directories + config"
mkdir -p ~/buddy/src ~/buddy/bin ~/buddy/config ~/.config/systemd/user
if [[ ! -f ~/buddy/config/buddy.env ]]; then
    cp buddy.env.example ~/buddy/config/buddy.env
fi
chmod 600 ~/buddy/config/buddy.env

echo "✓ setup done"
