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
    libasound2-dev libcurl4-openssl-dev curl patchelf \
    pulseaudio-utils

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
# Patched (-buddy1): libpiper hard-codes 1 ONNX thread; ours reads PIPER_INTRA_OP_THREADS (buddy-voice sets it
# from BUDDY_VOICE_THREADS). 3 threads: first audio ~830 → ~450 ms on the Pi 5 (measured 2026-10-23).
PIPER_VERSION=1.8.0
PIPER_BUILD="$PIPER_VERSION-buddy1"
PIPER_SHA256=0a22987a6157f14e2fb7cf4aa702b547467fd0448bea9f233abd503c3ff4a0e1  # source tarball
echo "→ libpiper $PIPER_BUILD → /opt/libpiper"
if [[ ! -f "/opt/libpiper-$PIPER_BUILD/lib/libpiper.so" ]]; then
    tmp="$(mktemp -d)"
    curl -fL --progress-bar -o "$tmp/piper.tgz" \
        "https://github.com/OHF-Voice/piper1-gpl/archive/refs/tags/v$PIPER_VERSION.tar.gz"
    echo "$PIPER_SHA256  $tmp/piper.tgz" | sha256sum -c --quiet
    tar xzf "$tmp/piper.tgz" -C "$tmp"
    src="$tmp/piper1-gpl-$PIPER_VERSION/libpiper/src/piper.cpp"
    sed -i 's|SetIntraOpNumThreads(1);|SetIntraOpNumThreads(getenv("PIPER_INTRA_OP_THREADS") ? atoi(getenv("PIPER_INTRA_OP_THREADS")) : 1);|' "$src"
    grep -q PIPER_INTRA_OP_THREADS "$src" || { echo "libpiper patch didn't apply (source changed?)" >&2; exit 1; }
    # DISABLE_FIND_PACKAGE: libpiper tries find_package(onnxruntime) first, which picks up the release's
    # broken CMake config (points at lib64/, see third_party/). Skipped, it uses ONNXRUNTIME_DIR directly.
    cmake -S "$tmp/piper1-gpl-$PIPER_VERSION/libpiper" -B "$tmp/build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_DISABLE_FIND_PACKAGE_onnxruntime=ON \
        -DONNXRUNTIME_DIR=/opt/onnxruntime \
        -DCMAKE_INSTALL_PREFIX="/opt/libpiper-$PIPER_BUILD" > /dev/null
    cmake --build "$tmp/build"
    sudo cmake --install "$tmp/build" > /dev/null
    rm -rf "$tmp"
fi
sudo ln -sfn "/opt/libpiper-$PIPER_BUILD" /opt/libpiper
# libpiper.so is installed without a RUNPATH, so it can't find libonnxruntime.so.1 (an executable's RUNPATH
# doesn't cover its libraries' dependencies). Give it one; $ORIGIN also covers libs next to it.
sudo patchelf --set-rpath '$ORIGIN:/opt/onnxruntime/lib' "/opt/libpiper-$PIPER_BUILD/lib/libpiper.so"

# Before 2026-10-23 Buddy used librespot (raspotify .deb); Spotify refuses it audio keys. Remove it.
if systemctl --user cat buddy-librespot >/dev/null 2>&1; then
    echo "→ removing librespot (replaced by Soloist)"
    systemctl --user disable --now buddy-librespot 2>/dev/null || true
    rm -rf ~/.config/systemd/user/buddy-librespot.service ~/.cache/librespot
fi
if dpkg-query -W raspotify >/dev/null 2>&1; then sudo apt-get purge -y raspotify; fi

# Echo cancellation + music sink for ducking (1.11). PipeWire only reads its config at start: restart it
# when the file changed. That briefly cuts all audio streams; ears reopens its device by itself.
echo "→ PipeWire: echo cancellation, music sink"
mkdir -p ~/.config/pipewire/pipewire.conf.d
if ! cmp -s pipewire/buddy-audio.conf ~/.config/pipewire/pipewire.conf.d/buddy-audio.conf; then
    install -m 644 pipewire/buddy-audio.conf ~/.config/pipewire/pipewire.conf.d/
    systemctl --user restart pipewire pipewire-pulse wireplumber
    sleep 2
    systemctl --user try-restart buddy-soloist
    echo "  installed, PipeWire restarted"
fi

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

# Spotify Connect (1.9): Spotify Soloist in ~/buddy/bin, kept current by a weekly timer, run sandboxed.
# Its key lives in soloist.env, not buddy.env (the sandbox hides everything else from it).
echo "→ Spotify Soloist"
bash ./soloist-update.sh
mkdir -p ~/.local/share/soloist ~/.cache/soloist  # the only folders the sandbox maps in
if [[ ! -f ~/buddy/config/soloist.env ]]; then
    cp soloist.env.example ~/buddy/config/soloist.env
fi
chmod 600 ~/buddy/config/soloist.env
# Key from the first Soloist version, which read it from buddy.env: move it over.
if key="$(grep -m1 -oP '^BUDDY_SOLOIST_API_KEY=\K.+' ~/buddy/config/buddy.env)"; then
    sed -i "s|^SOLOIST_API_KEY=.*|SOLOIST_API_KEY=$key|" ~/buddy/config/soloist.env
    sed -i '/^BUDDY_SOLOIST_API_KEY=/d' ~/buddy/config/buddy.env
    echo "  moved BUDDY_SOLOIST_API_KEY from buddy.env to soloist.env"
fi
install -m 644 systemd/buddy-soloist.service systemd/buddy-soloist-update.service \
    systemd/buddy-soloist-update.timer ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now buddy-soloist-update.timer
systemctl --user enable buddy-soloist
if grep -q '^SOLOIST_API_KEY=.' ~/buddy/config/soloist.env; then
    systemctl --user restart buddy-soloist
else
    echo "  ⚠ SOLOIST_API_KEY not set in ~/buddy/config/soloist.env: buddy-soloist not started"
fi

echo "→ models (wake word, VAD, speech-to-text, voice)"
bash ./fetch-models.sh

echo "✓ setup done"
