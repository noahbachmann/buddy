#!/usr/bin/env bash
# Record a voice session for speech-to-text training (docs/voice-training.md). Runs on the PC: prompts show here,
# the Pi records from the Jabra — raw and echo-cancelled at the same time — into ~/buddy/voice/<date>-<label>/.
# deploy/train.sh pulls the sessions to the PC.
#
# Usage: deploy/record.sh <label> [train|test] [wake count]
#   label   what's special about this session, e.g. desk, far, box, box-loud (box = music box playing)
#   set     which sentences of tools/voice/commands.tsv: train (default) or test (rephrasings, for scoring only)
#   wake    how many "Hey Jarvis" to record as well (default 20, 0 = none)
# Same label on the same day continues the session: sentences already recorded are skipped.
#
# Per sentence: Enter = start, Enter = stop (after you finished speaking), r = redo the last clip, s = skip,
# q = quit. Env: BUDDY_HOST (default "buddy")
set -euo pipefail
HOST="${BUDDY_HOST:-buddy}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
label="${1:?usage: deploy/record.sh <label> [train|test] [wake count]}"
set="${2:-train}"
wake="${3:-20}"
[[ "$label" =~ ^[a-z0-9-]+$ ]] || { echo "label: lowercase letters, digits and - only" >&2; exit 2; }
[[ "$set" == train || "$set" == test ]] || { echo "set must be train or test" >&2; exit 2; }
session="$(date +%F)-$label"

# Prompts: the set's sentences in random order (variety), then the wake word. "set<TAB>sentence" per line.
prompts="$(awk -F'\t' -v set="$set" '$1 == set { print $1 "\t" $2 }' "$ROOT/tools/voice/commands.tsv" | shuf)"
wake_line="$(awk -F'\t' '$1 == "wake" { print $1 "\t" $2; exit }' "$ROOT/tools/voice/commands.tsv")"
for ((i = 0; i < wake; i++)); do prompts+=$'\n'"$wake_line"; done

# shellcheck disable=SC2029  # $session expands here on purpose
ssh "$HOST" "mkdir -p ~/buddy/voice/$session && cat > ~/buddy/voice/$session/prompts.tsv" <<< "$prompts"

# The recording loop runs on the Pi; passed inline so it never depends on what was deployed.
read -r -d '' REMOTE <<'EOF' || true
set -euo pipefail
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
dir=~/buddy/voice/"$1"
mkdir -p "$dir/raw" "$dir/aec"
[[ -f "$dir/metadata.csv" ]] || echo "file,sentence,set" > "$dir/metadata.csv"

# Levels: peak dBFS of a 16-bit WAV
peak() { python3 -I -c 'import sys, wave, array, math
w = wave.open(sys.argv[1]); a = array.array("h", w.readframes(w.getnframes()))
print(round(20 * math.log10(max(max(map(abs, a), default=0), 1) / 32768)))' "$1"; }

# Next number = highest + 1 (never the count: a deleted clip must not make the next one overwrite another)
last="$(find "$dir/aec" -name '[0-9]*.wav' -printf '%f\n' | sort | tail -1)"
n=0
[[ -n "$last" ]] && n=$((10#${last%.wav}))

# Sentences already in this session are done, except the wake word (recorded many times)
mapfile -t prompts < "$dir/prompts.tsv"
todo=()
for p in "${prompts[@]}"; do
    s="${p#*$'\t'}"
    [[ "${p%%$'\t'*}" == wake ]] || ! grep -qF ",\"${s//\"/\"\"}\"," "$dir/metadata.csv" && todo+=("$p")
done
recorded_wake=$(grep -c ',wake$' "$dir/metadata.csv" || true)
echo "Session $1: ${#todo[@]} prompts left ($recorded_wake wake words already). Speak like you talk to Buddy."
echo "Enter = start, Enter = stop, r = redo last, s = skip, q = quit."

i=0
while ((i < ${#todo[@]})); do
    set_="${todo[i]%%$'\t'*}"
    text="${todo[i]#*$'\t'}"
    printf '\n  [%d/%d] "%s"\n' $((i + 1)) ${#todo[@]} "$text"
    read -r -p "  ready? " key
    case "$key" in
        q) break ;;
        s) i=$((i + 1)); continue ;;
        r)
            if ((n > 0)) && tail -1 "$dir/metadata.csv" | grep -q "^$(printf '%05d' $n).wav,"; then
                prev="$(tail -1 "$dir/metadata.csv")"
                sed -i '$d' "$dir/metadata.csv"
                rm -f "$dir/raw/$(printf '%05d' $n).wav" "$dir/aec/$(printf '%05d' $n).wav"
                n=$((n - 1))
                i=$((i > 0 ? i - 1 : 0))
                echo "  removed the last clip ($prev)"
            fi
            continue ;;
    esac
    file="$(printf '%05d' $((n + 1))).wav"
    pw-record --rate 16000 --channels 1 --format s16 "$dir/raw/$file" &
    raw=$!
    pw-record --target buddy_aec_source --rate 16000 --channels 1 --format s16 "$dir/aec/$file" &
    aec=$!
    read -r -p "  ● recording, Enter to stop " _
    sleep 0.3  # don't cut the last word
    kill -INT "$raw" "$aec"; wait "$raw" "$aec" 2>/dev/null || true
    n=$((n + 1))
    echo "$file,\"${text//\"/\"\"}\",$set_" >> "$dir/metadata.csv"
    p_aec="$(peak "$dir/aec/$file")"; p_raw="$(peak "$dir/raw/$file")"
    warn=""
    ((p_aec < -30)) && warn="  ⚠ quiet: speak up or move closer, then r"
    ((p_aec > -1 || p_raw > -1)) && warn="  ⚠ clipped: a bit softer, then r"
    echo "  ✓ $file  peak aec ${p_aec} dB, raw ${p_raw} dB$warn"
    i=$((i + 1))
done
echo
echo "✓ $(($(wc -l < "$dir/metadata.csv") - 1)) clips in $dir. Pull them with deploy/train.sh pull"
EOF
ssh -t "$HOST" "bash -c $(printf '%q' "$REMOTE") record $(printf '%q' "$session")"
