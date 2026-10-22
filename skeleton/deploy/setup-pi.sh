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
    libpaho-mqtt-dev libpaho-mqttpp-dev libssl-dev nlohmann-json3-dev \
    libasound2-dev curl

# Not Debian's libonnxruntime-dev: it prints ~570 "Schema error" lines on every start (see third_party/).
ORT_VERSION=1.30.0
ORT_SHA256=e16a27a8ed330bbc698df7330b0cf56e722f354e3bcc92118682c74ef3c3e3da  # aarch64 tarball
echo "→ ONNX Runtime $ORT_VERSION → /opt/onnxruntime"
[[ "$(uname -m)" == aarch64 ]] || { echo "expected aarch64, got $(uname -m)" >&2; exit 1; }
if [[ ! -f "/opt/onnxruntime-$ORT_VERSION/lib/libonnxruntime.so" ]]; then
    tmp="$(mktemp -d)"
    curl -fL --progress-bar -o "$tmp/ort.tgz" \
        "https://github.com/microsoft/onnxruntime/releases/download/v$ORT_VERSION/onnxruntime-linux-aarch64-$ORT_VERSION.tgz"
    echo "$ORT_SHA256  $tmp/ort.tgz" | sha256sum -c --quiet
    sudo tar xzf "$tmp/ort.tgz" -C /opt
    sudo mv "/opt/onnxruntime-linux-aarch64-$ORT_VERSION" "/opt/onnxruntime-$ORT_VERSION"
    rm -rf "$tmp"
fi
sudo ln -sfn "/opt/onnxruntime-$ORT_VERSION" /opt/onnxruntime

echo "→ mosquitto (localhost only for now)"
sudo systemctl enable --now mosquitto

echo "→ lingering: user services start at boot and survive SSH logout"
sudo loginctl enable-linger "$USER"

echo "→ directories + config"
mkdir -p ~/buddy/dev ~/buddy/src ~/buddy/bin ~/buddy/config ~/buddy/models ~/.config/systemd/user
if [[ ! -f ~/buddy/config/buddy.env ]]; then
    cp buddy.env.example ~/buddy/config/buddy.env
fi
chmod 600 ~/buddy/config/buddy.env

echo "→ models (wake word, VAD, speech-to-text)"
bash ./fetch-models.sh

echo "✓ setup done"
