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
    libasound2-dev libcurl4-openssl-dev curl

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

# libpiper (1.10): Piper's C/C++ library, built from source against the ONNX Runtime above. Its build
# also downloads and compiles espeak-ng (phonemes) via git. ⚠️ takes several minutes on the Pi.
PIPER_VERSION=1.8.0
PIPER_SHA256=0a22987a6157f14e2fb7cf4aa702b547467fd0448bea9f233abd503c3ff4a0e1  # source tarball
echo "→ libpiper $PIPER_VERSION → /opt/libpiper"
if [[ ! -f "/opt/libpiper-$PIPER_VERSION/lib/libpiper.so" ]]; then
    tmp="$(mktemp -d)"
    curl -fL --progress-bar -o "$tmp/piper.tgz" \
        "https://github.com/OHF-Voice/piper1-gpl/archive/refs/tags/v$PIPER_VERSION.tar.gz"
    echo "$PIPER_SHA256  $tmp/piper.tgz" | sha256sum -c --quiet
    tar xzf "$tmp/piper.tgz" -C "$tmp"
    cmake -S "$tmp/piper1-gpl-$PIPER_VERSION/libpiper" -B "$tmp/build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DONNXRUNTIME_DIR=/opt/onnxruntime \
        -DCMAKE_INSTALL_PREFIX="/opt/libpiper-$PIPER_VERSION" > /dev/null
    cmake --build "$tmp/build"
    sudo cmake --install "$tmp/build" > /dev/null
    rm -rf "$tmp"
fi
sudo ln -sfn "/opt/libpiper-$PIPER_VERSION" /opt/libpiper

# Spotify Connect (1.9): raspotify's .deb only for its librespot binary. Pinned like ONNX Runtime;
# to update, change all three (https://github.com/dtcooper/raspotify/releases).
RASPOTIFY_VERSION=0.48.4
RASPOTIFY_DEB=raspotify_0.48.4.librespot.v0.8.0-99b6088_arm64.deb
RASPOTIFY_SHA256=4d272678f475ad18961563951b6c36e5395c8a85b8d5607d1e3e9f94671108e2
echo "→ librespot (raspotify $RASPOTIFY_VERSION)"
if [[ "$(dpkg-query -W -f='${Version}' raspotify 2>/dev/null || true)" != "$RASPOTIFY_VERSION"~* ]]; then
    tmp="$(mktemp -d)"
    curl -fL --progress-bar -o "$tmp/$RASPOTIFY_DEB" \
        "https://github.com/dtcooper/raspotify/releases/download/$RASPOTIFY_VERSION/$RASPOTIFY_DEB"
    echo "$RASPOTIFY_SHA256  $tmp/$RASPOTIFY_DEB" | sha256sum -c --quiet
    sudo apt-get install -y "$tmp/$RASPOTIFY_DEB"
    rm -rf "$tmp"
fi
# Its system service can't reach our PipeWire session; buddy-librespot (user unit) replaces it.
sudo systemctl disable --now raspotify 2>/dev/null || true

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

echo "→ librespot user unit"
install -m 644 systemd/buddy-librespot.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now buddy-librespot

echo "→ models (wake word, VAD, speech-to-text, voice)"
bash ./fetch-models.sh

echo "✓ setup done"
