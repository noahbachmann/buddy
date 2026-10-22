# 1.7 – Speech-to-text

> Written 2026-10-22. VAD end-of-speech logic tested in a Debian 13 container; **whisper.cpp not yet run** (models were unreachable from the build sandbox). Items marked ⚠️ are assumptions, especially all Pi 5 timings.

## Goal

After the wake word, Buddy records until you stop talking, turns it into text and publishes it: "Play Spotify" arrives on `buddy/ears/transcript` within ~1–2 s after you finish speaking.

## Prerequisites

- 1.6 done: wake word triggers reliably; `buddy@ears` deployed.
- `ggml-base.en.bin` in `~/buddy/models` (1.6 step 1 downloaded it).

## Parts / tools

| Tool | Where | Role |
|---|---|---|
| Silero VAD v6.2.3 (`silero_vad.onnx`) | `~/buddy/models`, run by ONNX Runtime | Speech probability per 32 ms: finds the end of the command |
| whisper.cpp v1.9.5 | Built from source by CMake (`third_party/`), static | Speech → text, on the Pi's CPU |
| GGML whisper models | `~/buddy/models` | `ggml-base.en.bin` default; others via `fetch-models.sh` |

## Background

After the wake word, ears switches from "listen for Jarvis" to "record the command":

```
 wake ─► record ──► VAD says speech ──► VAD says silence for 700 ms ──► whisper ──► MQTT
            │                                                │
            └─ nothing said for 4 s → give up               └─ or 10 s max
```

- **Why VAD:** whisper transcribes a finished clip; it doesn't know when you stopped. Silero VAD scores each 32 ms as speech/not speech. `BUDDY_VAD_SILENCE_MS` (700) is the pause that ends a command, and it's added directly to the latency. Shorter = snappier, but "play… uh… Spotify" gets cut.
- **Latency budget:** 700 ms silence + whisper time. whisper always encodes a 30 s window, so time depends mostly on model size, not on command length. Rough Pi 5 guesses ⚠️ (measure in step 2):

| Model | Size | Languages | Expected time per command ⚠️ |
|---|---|---|---|
| `ggml-tiny.en.bin` | 75 MB | English | ~0.3–0.6 s |
| `ggml-base.en.bin` (default) | 142 MB | English | ~0.7–1.5 s |
| `ggml-small.en.bin` | 466 MB | English | ~3–5 s (too slow) |
| `ggml-base.bin` | 142 MB | multi, e.g. German | like base.en, slightly worse English |

- **English vs High German:** `.en` models are English only and better at it. For German: multilingual model + `BUDDY_STT_LANGUAGE=de`. Avoid `auto`: language detection on 1-second commands is unreliable.

### MQTT contract

```
buddy/ears/transcript {"text":"Play Spotify.","language":"en","audio_ms":2160,"stt_ms":910}
```

`text` comes verbatim from whisper (capitals, punctuation). Normalizing is the brain's job in 1.8.

## Steps

### 1. Offline test

Record a few commands (wake word + command each, pause between):

```bash
ssh buddy
pw-record --rate 16000 --channels 1 --format s16 ~/cmds.wav
# "Hey Jarvis … play Spotify"   (wait 2 s)
# "Hey Jarvis … pause"          (wait 2 s)
# "Hey Jarvis … next song"      (wait 2 s)
# "Hey Jarvis … louder"         Ctrl+C after 2 s
~/buddy/bin/buddy-ears --wav ~/cmds.wav
```

Expected ⚠️:

```
[ears] 1.84 s wake word (score 0.93)
[ears] 4.40 s "Play Spotify." (2160 ms audio, stt 910 ms)
  → buddy/ears/transcript {"audio_ms":2160,"language":"en","stt_ms":910,"text":"Play Spotify."}
[ears] 7.12 s wake word (score 0.95)
[ears] 9.04 s "Pause." (1520 ms audio, stt 870 ms)
...
```

### 2. Pick the model by speed vs accuracy

Fetch the alternatives and compare them on the **same** recording:

```bash
bash ~/buddy/src/deploy/fetch-models.sh ggml-tiny.en.bin ggml-small.en.bin
for m in ggml-tiny.en.bin ggml-base.en.bin ggml-small.en.bin; do
    echo "== $m"; BUDDY_STT_MODEL=$m ~/buddy/bin/buddy-ears --wav ~/cmds.wav | grep '"'
done
```

Pick the smallest model that gets all commands right, with `stt` ≤ ~1000 ms. Write it into `~/buddy/config/buddy.env`:

```bash
BUDDY_STT_MODEL=ggml-base.en.bin
```

Optional knobs (same file): `BUDDY_STT_PROMPT=Spotify, Buddy` nudges spelling of names; `BUDDY_STT_THREADS=4` (the Pi 5 has 4 cores, keep it).

For German instead: `fetch-models.sh ggml-base.bin`, then `BUDDY_STT_MODEL=ggml-base.bin` and `BUDDY_STT_LANGUAGE=de`.

### 3. Live, with end-to-end timing

```bash
ssh buddy systemctl --user restart buddy@ears
ssh buddy "mosquitto_sub -v -F '%I %t %p' -t 'buddy/ears/#'"
```

`%I` prints an ISO timestamp per message. Say "Hey Jarvis, play Spotify" from your chair. Expected ⚠️:

```
2026-10-23T19:02:11+0200 buddy/ears/wake {"model":"hey_jarvis_v0.1","score":0.94}
2026-10-23T19:02:11+0200 buddy/ears/state {"state":"listening"}
2026-10-23T19:02:13+0200 buddy/ears/state {"state":"transcribing"}
2026-10-23T19:02:14+0200 buddy/ears/transcript {"audio_ms":2240,"language":"en","stt_ms":930,"text":"Play Spotify."}
2026-10-23T19:02:14+0200 buddy/ears/state {"state":"idle"}
```

Latency after you stop talking ≈ `BUDDY_VAD_SILENCE_MS` + `stt_ms`. Target ≤ 2000 ms.

### 4. Tune end-of-speech (only if needed)

| Problem | Change in `buddy.env` |
|---|---|
| Commands cut off mid-sentence | `BUDDY_VAD_SILENCE_MS=900` |
| Feels sluggish, commands are short | `BUDDY_VAD_SILENCE_MS=500` |
| Fan/room noise keeps it "listening" until 10 s | `BUDDY_VAD_THRESHOLD=0.6` |
| Quiet speech from far away not detected | `BUDDY_VAD_THRESHOLD=0.4` |

Restart after each change: `ssh buddy systemctl --user restart buddy@ears`.

## Verify

- [ ] `--wav` test: all four commands transcribed correctly.
- [ ] Model chosen, `stt_ms` noted, set in `buddy.env`.
- [ ] Live from 2–3 m: "play Spotify", "pause", "next song", "louder", "quieter" each arrive as `buddy/ears/transcript` with the right text, ≥ 9 of 10.
- [ ] End of speech → transcript ≤ ~2 s (mosquitto_sub timestamps).
- [ ] Wake word + silence → `no command after wake word`, nothing published, back to `idle`.

**Done when:** "Play Spotify" arrives on MQTT within ~1–2 s.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `cannot load whisper model …` | Model missing / name typo | `ls ~/buddy/models`; `fetch-models.sh <name>` |
| Transcript empty or `"you"` / `"Thank you."` | whisper hallucinating on silence or noise: VAD ended too late or the speech was too quiet | Check the Jabra volume (1.4 step 5); raise `BUDDY_VAD_THRESHOLD` |
| First word missing ("lay Spotify") | Speaking before the wake word finished triggering | Small pause after "Hey Jarvis"; or lower `BUDDY_WAKE_PATIENCE` to 1 |
| `stt` several seconds | Model too big, or Debug build | Smaller model; deployed binary is Release (whisper is `-O2` even in Debug, but check) |
| `stt` gets slower over minutes | CPU throttling | `vcgencmd measure_temp`, `vcgencmd get_throttled` must be `0x0`; Active Cooler fitted? |
| German words in English output | `.en` model | Multilingual model + `BUDDY_STT_LANGUAGE=de` |
| Wake word missed right after a command | Detector warms up 1.3 s after each command | Expected; wait a moment before the next "Hey Jarvis" |

## Notes for later phases

- **1.8 brain:** subscribes to `buddy/ears/transcript`. Lowercase and strip punctuation before matching. Expect whisper variants ("Play Spotify", "Play, Spotify!", "play spotty fi").
- **1.10 voice:** while Buddy speaks, ears hears it. Plan: ears ignores wake words while `buddy/voice/state` = speaking, or 1.11 echo-cancel. Decide in 1.10/1.11.
- **2.4 eyes:** `buddy/ears/state` → `listening` / `transcribing` map directly to eye animations.
- **Faster later if needed:** whisper's `audio_ctx` parameter (shorter encoder window for short clips), or a quantized model (`ggml-base.en-q5_1.bin`). Measure first.
- **Memory:** base model ≈ 200–300 MB RSS ⚠️; check `systemctl --user status buddy@ears` once Spotify and TTS also run.
