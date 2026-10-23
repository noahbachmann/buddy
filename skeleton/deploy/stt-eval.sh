#!/usr/bin/env bash
# Compare speech-to-text models on recorded clips (1.11, optional; guides/01-11-custom-stt.md). Runs ON the Pi.
# Use clips the model was NOT trained on (e.g. a separate record-voice.sh ~/buddy/voice-test run).
#
# Usage: stt-eval.sh <dir> <model.bin>…   e.g. stt-eval.sh ~/buddy/voice-test ggml-base.en.bin ggml-buddy.bin
# A clip counts as right when the text matches after lowercasing and dropping punctuation (what the brain
# does, roughly). Prints the wrong ones, then a score per model.
set -euo pipefail
DIR="${1:?clip dir with metadata.csv}"; shift
[[ $# -gt 0 ]] || { echo "usage: $0 <dir> <model.bin>…" >&2; exit 2; }
EARS="${BUDDY_EARS_BIN:-$HOME/buddy/bin/buddy-ears}"

# Same settings as the live service (language, prompt, threads). Not `source`: values like
# "BUDDY_STT_PROMPT=Spotify, Buddy" are unquoted, systemd style.
if [[ -f ~/buddy/config/buddy.env ]]; then
    while IFS='=' read -r key value; do
        [[ "$key" =~ ^BUDDY_[A-Z0-9_]+$ ]] && export "$key=$value"
    done < ~/buddy/config/buddy.env
fi

norm() { tr '[:upper:]' '[:lower:]' <<< "$1" | tr -c 'a-z0-9\n' ' ' | tr -s ' ' | sed 's/^ //; s/ $//'; }

declare -A expected
files=()
while IFS= read -r line; do
    [[ "$line" =~ ^([^,]+),\"(.*)\"$ ]] || continue
    text="${BASH_REMATCH[2]//\"\"/\"}"
    expected["$DIR/${BASH_REMATCH[1]}"]="$text"
    files+=("$DIR/${BASH_REMATCH[1]}")
done < "$DIR/metadata.csv"
echo "${#files[@]} clips"

for model in "$@"; do
    echo
    echo "== $model"
    right=0 total=0 ms=0
    while IFS=$'\t' read -r file t text; do
        total=$((total + 1)); ms=$((ms + t))
        if [[ "$(norm "$text")" == "$(norm "${expected[$file]}")" ]]; then
            right=$((right + 1))
        else
            printf '  ✗ %s\n      said: %s\n     heard: %s\n' "${file##*/}" "${expected[$file]}" "$text"
        fi
    done < <(BUDDY_STT_MODEL="$model" "$EARS" --transcribe "${files[@]}" 2>/dev/null)
    ((total > 0)) || { echo "  no output: does $model exist in ~/buddy/models?"; continue; }
    echo "  → $right / $total right, avg stt $((ms / total)) ms"
done
