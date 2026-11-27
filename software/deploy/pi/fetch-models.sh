#!/usr/bin/env bash
# Download the ML models into ~/buddy/models (not in Git: too big). Runs ON the Pi; setup.sh calls it.
# Idempotent: existing files are kept.
#
# Usage: fetch-models.sh [model ...]
#   no model   the fixed models + what ~/buddy/config/buddy.env names (BUDDY_WAKE_MODEL, BUDDY_STT_MODEL,
#              BUDDY_VOICE_MODEL; unset = the services' defaults)
#   model      whisper ggml-*.bin (e.g. ggml-small.en.bin; list: https://huggingface.co/ggerganov/whisper.cpp/tree/main)
#              or a Piper voice <locale>-<name>-<quality> (e.g. de_DE-thorsten-medium; https://rhasspy.github.io/piper-samples/)
# Your own models (fine-tuned whisper ggml-buddy-*, a custom wake word) can't be downloaded: copy them from the PC.
set -euo pipefail

# Not `source`: buddy.env values are unquoted, systemd style. Trailing whitespace and "\r" (edited on Windows) are
# dropped, like the services do.
if [[ -f ~/buddy/config/buddy.env ]]; then
    while IFS='=' read -r key value; do
        [[ "$key" =~ ^BUDDY_[A-Z0-9_]+$ ]] && export "$key=${value%"${value##*[![:space:]]}"}"
    done < ~/buddy/config/buddy.env
fi
DIR="${BUDDY_MODELS_DIR:-$HOME/buddy/models}"
mkdir -p "$DIR"

fetch() {  # url file [sha256]
    local url="$1" file="$DIR/$2" sha="${3:-}"
    if [[ ! -f "$file" ]]; then
        echo "  ↓ $2"
        curl -fL --progress-bar -o "$file.part" "$url"
        mv "$file.part" "$file"
    fi
    if [[ -n "$sha" ]] && ! echo "$sha  $file" | sha256sum -c --quiet; then
        echo "  ✗ $2: checksum mismatch, deleted. Run again." >&2
        rm -f "$file"
        exit 1
    fi
    printf '  ✓ %-30s %s\n' "$2" "$(du -h "$file" | cut -f1)"
}

own() {  # file: a model only the PC has
    if [[ -f "$DIR/$1" ]]; then
        printf '  ✓ %-30s %s (yours)\n' "$1" "$(du -h "$DIR/$1" | cut -f1)"
    else
        echo "  ✗ $1: your own model, can't be downloaded. On the PC: scp $1 buddy:buddy/models/" >&2
        missing=1
    fi
}

# openWakeWord v0.5.1: feature models (every wake word needs them) + stock "hey jarvis"
OWW=https://github.com/dscripka/openWakeWord/releases/download/v0.5.1
fetch "$OWW/melspectrogram.onnx"  melspectrogram.onnx  ba2b0e0f8b7b875369a2c89cb13360ff53bac436f2895cced9f479fa65eb176f
fetch "$OWW/embedding_model.onnx" embedding_model.onnx 70d164290c1d095d1d4ee149bc5e00543250a7316b59f31d056cff7bd3075c1f
fetch "$OWW/hey_jarvis_v0.1.onnx" hey_jarvis_v0.1.onnx 94a13cfe60075b132f6a472e7e462e8123ee70861bc3fb58434a73712ee0d2cb

# Silero VAD v6.2.3
fetch https://raw.githubusercontent.com/snakers4/silero-vad/v6.2.3/src/silero_vad/data/silero_vad.onnx \
    silero_vad.onnx 1a153a22f4509e292a94e67d6f9b85e8deb25b4988682b7e174c65279d8788e3

missing=0
models=("$@")
if [[ ${#models[@]} -eq 0 ]]; then
    wake="${BUDDY_WAKE_MODEL:-hey_jarvis_v0.1.onnx}"
    [[ "$wake" == hey_jarvis_v0.1.onnx ]] || own "$wake"
    voice="${BUDDY_VOICE_MODEL:-en_GB-alan-medium.onnx}"
    models=("${BUDDY_STT_MODEL:-ggml-base.en.bin}" "${voice%.onnx}")
fi

# whisper.cpp GGML models and Piper voices (no pinned checksums: Hugging Face serves the current file)
PIPER=https://huggingface.co/rhasspy/piper-voices/resolve/main
for m in "${models[@]}"; do
    if [[ "$m" == ggml-buddy* ]]; then
        own "$m"
    elif [[ "$m" == ggml-*.bin ]]; then
        fetch "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/$m" "$m"
    elif [[ "$m" =~ ^(([a-z]+)_[A-Z]+)-(.+)-(x_low|low|medium|high)$ ]]; then
        # en_GB-alan-medium → en/en_GB/alan/medium/en_GB-alan-medium.onnx (+ .onnx.json)
        dir="$PIPER/${BASH_REMATCH[2]}/${BASH_REMATCH[1]}/${BASH_REMATCH[3]}/${BASH_REMATCH[4]}"
        fetch "$dir/$m.onnx" "$m.onnx"
        fetch "$dir/$m.onnx.json" "$m.onnx.json"
    else
        echo "  ✗ $m: not a whisper model (ggml-*.bin) or Piper voice (en_GB-alan-medium)" >&2
        exit 1
    fi
done
exit "$missing"
