#!/usr/bin/env bash
# Pi setup for Buddy. Runs ON the Pi; `deploy/deploy.sh --setup` calls it for you. Idempotent: run it again
# after pulling changes to deploy/.
#
# Usage: setup.sh [step ...]     no step = all, in this order:
#   packages     apt build tools and libraries
#   onnxruntime  Microsoft's prebuilt ONNX Runtime → /opt/onnxruntime (D9)
#   libpiper     Piper TTS library built from source → /opt/libpiper (D12)
#   audio        PipeWire echo cancellation + bigger USB buffers (D13); restarts PipeWire only if changed
#   mosquitto    MQTT broker, localhost only (D2)
#   session      lingering: user services start at boot without a login (D7)
#   config       ~/buddy folders, buddy.env from the example
#   models       deploy/pi/fetch-models.sh: what buddy.env names
#   cleanup      removes what earlier versions of Buddy installed
#   check        warns about keys in buddy.env that no service reads (typos, removed settings)
set -euo pipefail
cd "$(dirname "$0")"
CONFIG=../config

# Pinned versions. Bump here, run `setup.sh <step>`, then deploy (services link against /opt).
ORT_VERSION=1.30.0
ORT_SHA256=e16a27a8ed330bbc698df7330b0cf56e722f354e3bcc92118682c74ef3c3e3da  # onnxruntime-linux-aarch64 tarball
PIPER_VERSION=1.8.0
PIPER_BUILD="$PIPER_VERSION-buddy1"  # -buddy1: patched thread count, see step_libpiper
PIPER_SHA256=0a22987a6157f14e2fb7cf4aa702b547467fd0448bea9f233abd503c3ff4a0e1  # source tarball

step_packages() {
    sudo apt-get update
    sudo apt-get install -y \
        build-essential cmake ninja-build gdb rsync git ccache pkg-config \
        mosquitto mosquitto-clients \
        libpaho-mqtt-dev libpaho-mqttpp-dev libssl-dev nlohmann-json3-dev \
        libasound2-dev libcurl4-openssl-dev curl patchelf
}

# Not Debian's libonnxruntime-dev: it prints ~570 "Schema error" lines on every start (see third_party/).
step_onnxruntime() {
    [[ "$(uname -m)" == aarch64 ]] || { echo "expected aarch64, got $(uname -m)" >&2; exit 1; }
    if [[ ! -f "/opt/onnxruntime-$ORT_VERSION/lib/libonnxruntime.so" ]]; then
        local tmp
        tmp="$(mktemp -d)"
        curl -fL --progress-bar -o "$tmp/ort.tgz" \
            "https://github.com/microsoft/onnxruntime/releases/download/v$ORT_VERSION/onnxruntime-linux-aarch64-$ORT_VERSION.tgz"
        echo "$ORT_SHA256  $tmp/ort.tgz" | sha256sum -c --quiet
        sudo tar xzf "$tmp/ort.tgz" -C /opt
        sudo mv "/opt/onnxruntime-linux-aarch64-$ORT_VERSION" "/opt/onnxruntime-$ORT_VERSION"
        rm -rf "$tmp"
    fi
    sudo ln -sfn "/opt/onnxruntime-$ORT_VERSION" /opt/onnxruntime
    echo "  ✓ onnxruntime $ORT_VERSION"
}

# Piper's C/C++ library, built against the ONNX Runtime above. Its build also downloads and compiles espeak-ng
# (phonemes) via git: several minutes on the Pi. Patched: libpiper hard-codes 1 ONNX thread; ours reads
# PIPER_INTRA_OP_THREADS (buddy-voice sets it from BUDDY_VOICE_THREADS; 3 threads: first audio ~830 → ~450 ms).
step_libpiper() {
    if [[ ! -f "/opt/libpiper-$PIPER_BUILD/lib/libpiper.so" ]]; then
        local tmp src
        tmp="$(mktemp -d)"
        curl -fL --progress-bar -o "$tmp/piper.tgz" \
            "https://github.com/OHF-Voice/piper1-gpl/archive/refs/tags/v$PIPER_VERSION.tar.gz"
        echo "$PIPER_SHA256  $tmp/piper.tgz" | sha256sum -c --quiet
        tar xzf "$tmp/piper.tgz" -C "$tmp"
        src="$tmp/piper1-gpl-$PIPER_VERSION/libpiper/src/piper.cpp"
        sed -i 's|SetIntraOpNumThreads(1);|SetIntraOpNumThreads(getenv("PIPER_INTRA_OP_THREADS") ? atoi(getenv("PIPER_INTRA_OP_THREADS")) : 1);|' "$src"
        grep -q PIPER_INTRA_OP_THREADS "$src" || { echo "libpiper patch didn't apply (source changed?)" >&2; exit 1; }
        # DISABLE_FIND_PACKAGE: find_package(onnxruntime) would pick up the release's broken CMake config
        # (points at lib64/, see third_party/). Skipped, libpiper uses ONNXRUNTIME_DIR directly.
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
    # Installed without a RUNPATH, so it can't find libonnxruntime.so.1 (an executable's RUNPATH doesn't cover its
    # libraries' dependencies). $ORIGIN also covers libs next to it.
    sudo patchelf --set-rpath '$ORIGIN:/opt/onnxruntime/lib' "/opt/libpiper-$PIPER_BUILD/lib/libpiper.so"
    echo "  ✓ libpiper $PIPER_BUILD"
}

# PipeWire only reads its config at start: restart it when a file changed. That briefly cuts all audio;
# ears and voice reopen their devices by themselves.
step_audio() {
    local pw=~/.config/pipewire/pipewire.conf.d wp=~/.config/wireplumber/wireplumber.conf.d
    mkdir -p "$pw" "$wp"
    if cmp -s "$CONFIG/pipewire/buddy-audio.conf" "$pw/buddy-audio.conf" \
        && cmp -s "$CONFIG/wireplumber/buddy-alsa.conf" "$wp/buddy-alsa.conf"; then
        echo "  ✓ unchanged"
        return
    fi
    install -m 644 "$CONFIG/pipewire/buddy-audio.conf" "$pw/"
    install -m 644 "$CONFIG/wireplumber/buddy-alsa.conf" "$wp/"
    systemctl --user restart pipewire pipewire-pulse wireplumber
    echo "  ✓ installed, PipeWire restarted"
}

step_mosquitto() {
    sudo systemctl enable --now mosquitto
}

step_session() {
    sudo loginctl enable-linger "$USER"
}

step_config() {
    mkdir -p ~/buddy/dev ~/buddy/src ~/buddy/bin ~/buddy/config ~/buddy/models ~/.config/systemd/user
    if [[ ! -f ~/buddy/config/buddy.env ]]; then
        cp "$CONFIG/buddy.env.example" ~/buddy/config/buddy.env
        echo "  created ~/buddy/config/buddy.env: set BUDDY_SPOTIFY_CLIENT_ID and BUDDY_SPOTIFY_DEVICE"
    fi
    chmod 600 ~/buddy/config/buddy.env
}

step_models() {
    bash ./fetch-models.sh
}

# Leftovers of earlier versions. An entry can go once the Pi has run it.
step_cleanup() {
    local unit
    for unit in buddy-soloist.service buddy-soloist-update.service buddy-soloist-update.timer \
        buddy@mixer.service buddy-librespot.service; do
        if systemctl --user cat "$unit" >/dev/null 2>&1; then
            systemctl --user disable --now "$unit" 2>/dev/null || true
            echo "  removed $unit"
        fi
        rm -f ~/.config/systemd/user/"$unit"
    done
    systemctl --user daemon-reload
    rm -rf ~/buddy/bin/soloist ~/buddy/bin/buddy-mixer ~/buddy/config/soloist.env \
        ~/.local/share/soloist ~/.cache/soloist ~/.cache/librespot
    if dpkg-query -W raspotify >/dev/null 2>&1; then sudo apt-get purge -y raspotify; fi
}

step_check() {
    local key known
    known="$(grep -oE '\bBUDDY_[A-Z0-9_]+' "$CONFIG/buddy.env.example" | sort -u)"
    while IFS='=' read -r key _; do
        [[ "$key" =~ ^BUDDY_[A-Z0-9_]+$ ]] || continue
        grep -qx "$key" <<< "$known" || echo "  ⚠ buddy.env: $key is not a known setting (typo or removed?)"
    done < ~/buddy/config/buddy.env
    grep -q '^BUDDY_SPOTIFY_DEVICE=.' ~/buddy/config/buddy.env \
        || echo "  ⚠ buddy.env: BUDDY_SPOTIFY_DEVICE is not set (the music box's name in the Spotify app)"
    echo "  ✓ checked"
}

ALL=(packages onnxruntime libpiper audio mosquitto session config models cleanup check)
steps=("$@")
[[ ${#steps[@]} -gt 0 ]] || steps=("${ALL[@]}")
for s in "${steps[@]}"; do
    declare -F "step_$s" > /dev/null || { echo "unknown step \"$s\", expected one of: ${ALL[*]}" >&2; exit 2; }
done
for s in "${steps[@]}"; do
    echo "→ $s"
    "step_$s"
done
echo "✓ setup done"
