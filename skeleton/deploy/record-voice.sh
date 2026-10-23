#!/usr/bin/env bash
# Record your voice for fine-tuning speech-to-text (1.11, optional; guides/01-11-custom-stt.md). Runs ON the Pi.
# Shows one sentence at a time: Enter starts the recording, Enter stops it. Records what ears hears
# (16 kHz mono, from the echo-cancelled mic), so the model trains on exactly that sound.
#
# Usage: record-voice.sh [dir] [prompts.txt]
#   dir      default ~/buddy/voice-data; clips/ + metadata.csv (Hugging Face "audiofolder" layout)
#   prompts  one sentence per line, "#" = comment; default: dir/prompts.txt, created from voice-prompts.txt
# Run it as often as you like: every run appends new clips (vary distance and loudness between runs).
set -euo pipefail
DIR="${1:-$HOME/buddy/voice-data}"
PROMPTS="${2:-$DIR/prompts.txt}"
SOURCE="${BUDDY_RECORD_SOURCE:-buddy_aec_source}"

mkdir -p "$DIR/clips"
if [[ ! -f "$PROMPTS" ]]; then
    cp "$(dirname "$0")/voice-prompts.txt" "$PROMPTS"
    echo "Created $PROMPTS: add your own artists and playlists there, then run again."
    exit 0
fi
[[ -f "$DIR/metadata.csv" ]] || echo "file_name,transcription" > "$DIR/metadata.csv"

mapfile -t lines < <(grep -v '^\s*\(#\|$\)' "$PROMPTS")
n=$(find "$DIR/clips" -name '*.wav' | wc -l)
echo "${#lines[@]} sentences, $n clips so far. Speak like you talk to Buddy, from where you usually sit."
echo "Enter = start/stop, s + Enter = skip, q + Enter = quit."

for text in "${lines[@]}"; do
    echo
    echo "  \"$text\""
    read -r -p "  ready? " key
    [[ "$key" == q ]] && break
    [[ "$key" == s ]] && continue
    n=$((n + 1))
    file="clips/$(printf '%05d' "$n").wav"
    pw-record --target "$SOURCE" --rate 16000 --channels 1 --format s16 "$DIR/$file" &
    pid=$!
    read -r -p "  ● recording, Enter to stop " _
    sleep 0.3  # don't cut the last word
    kill -INT "$pid"; wait "$pid" 2>/dev/null || true
    # CSV: quote the text, double any quotes inside
    echo "$file,\"${text//\"/\"\"}\"" >> "$DIR/metadata.csv"
done

echo
echo "✓ $(($(wc -l < "$DIR/metadata.csv") - 1)) clips in $DIR"
