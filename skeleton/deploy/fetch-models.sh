#!/usr/bin/env bash
# Download the ML models ears and voice need into ~/buddy/models (not in Git: too big). Runs ON the Pi; setup-pi.sh calls it.
# Idempotent: existing files are kept.
#
# Usage: fetch-models.sh [model ...]
#   whisper model  ggml-*.bin, e.g. ggml-tiny.en.bin, ggml-small.en.bin, ggml-base.bin (multilingual)
#                  List: https://huggingface.co/ggerganov/whisper.cpp/tree/main
#   Piper voice    <locale>-<name>-<quality>, e.g. en_GB-alan-medium, de_DE-thorsten-medium
#                  List and samples: https://rhasspy.github.io/piper-samples/
#   default: ggml-base.en.bin en_US-lessac-medium
set -euo pipefail
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
    printf '  ✓ %-28s %s\n' "$2" "$(du -h "$file" | cut -f1)"
}

# openWakeWord v0.5.1: feature models + stock "hey jarvis" wake word
OWW=https://github.com/dscripka/openWakeWord/releases/download/v0.5.1
fetch "$OWW/melspectrogram.onnx"  melspectrogram.onnx  ba2b0e0f8b7b875369a2c89cb13360ff53bac436f2895cced9f479fa65eb176f
fetch "$OWW/embedding_model.onnx" embedding_model.onnx 70d164290c1d095d1d4ee149bc5e00543250a7316b59f31d056cff7bd3075c1f
fetch "$OWW/hey_jarvis_v0.1.onnx" hey_jarvis_v0.1.onnx 94a13cfe60075b132f6a472e7e462e8123ee70861bc3fb58434a73712ee0d2cb

# Silero VAD v6.2.3
fetch https://raw.githubusercontent.com/snakers4/silero-vad/v6.2.3/src/silero_vad/data/silero_vad.onnx \
    silero_vad.onnx 1a153a22f4509e292a94e67d6f9b85e8deb25b4988682b7e174c65279d8788e3

# whisper.cpp GGML models and Piper voices (no pinned checksums: Hugging Face serves the current file)
PIPER=https://huggingface.co/rhasspy/piper-voices/resolve/main
models=("$@")
[[ ${#models[@]} -eq 0 ]] && models=(ggml-base.en.bin en_US-lessac-medium)
for m in "${models[@]}"; do
    if [[ "$m" == ggml-*.bin ]]; then
        fetch "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/$m" "$m"
    elif [[ "$m" =~ ^(([a-z]+)_[A-Z]+)-(.+)-(x_low|low|medium|high)$ ]]; then
        # en_US-lessac-medium → en/en_US/lessac/medium/en_US-lessac-medium.onnx (+ .onnx.json)
        dir="$PIPER/${BASH_REMATCH[2]}/${BASH_REMATCH[1]}/${BASH_REMATCH[3]}/${BASH_REMATCH[4]}"
        fetch "$dir/$m.onnx" "$m.onnx"
        fetch "$dir/$m.onnx.json" "$m.onnx.json"
    else
        echo "  ✗ $m: not a whisper model (ggml-*.bin) or Piper voice (en_US-lessac-medium)" >&2
        exit 1
    fi
done
