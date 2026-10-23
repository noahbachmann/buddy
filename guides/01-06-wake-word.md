# 1.6 – Wake word

> Written 2026-10-22. Code compiled and tested in a Debian 13 container (x86_64) against a synthesized "Hey Jarvis" clip, **not yet on the Pi with the Jabra**. Items marked ⚠️ are assumptions.

## Goal

Buddy reacts to a wake word spoken from 2–3 m away, with few false triggers. Stock openWakeWord model **"hey jarvis"** first; a custom "Hey buddy" model is optional (end of this guide).

## Prerequisites

- 1.4 done: Jabra is PipeWire's default source; a 16 kHz recording sounds clear.
- 1.5 done: Mosquitto runs, `deploy/deploy.sh` works, CLion Full Remote Mode builds on the Pi.
- Branch `ears-wake-word-stt` checked out (or merged).

## Parts / tools

| Tool | Where | Role |
|---|---|---|
| `buddy-ears` | `services/ears/` | The service: microphone → wake word → (1.7) VAD → text |
| ONNX Runtime 1.30.0 (prebuilt, aarch64) | `/opt/onnxruntime` on the Pi | Runs the wake word and VAD models |
| openWakeWord v0.5.1 models | `~/buddy/models` | `melspectrogram.onnx`, `embedding_model.onnx`, `hey_jarvis_v0.1.onnx` |
| ALSA (`libasound2-dev`) | Pi | Capture API; the `default` device goes through PipeWire (`pipewire-alsa`) |

## Background: how the wake word works

openWakeWord isn't one model, it's three in a row. Every 80 ms (1280 samples):

```
 80 ms audio ─► melspectrogram.onnx ─► 8 frames "how loud is each pitch band"
 last 0.76 s of frames ─► embedding_model.onnx ─► 96 numbers "what does this sound like"   (Google's speech model)
 last 1.28 s of embeddings ─► hey_jarvis_v0.1.onnx ─► score 0..1 "was that 'hey jarvis'?"
```

The first two models are the same for every wake word, so a custom "Hey buddy" model is just a small third file. A trigger needs the score ≥ `BUDDY_WAKE_THRESHOLD` (0.5) for `BUDDY_WAKE_PATIENCE` (2) chunks in a row. **Threshold up = fewer false triggers, but you must speak more clearly.**

### Decisions made in this step

| Topic | Choice | Why |
|---|---|---|
| Capture API | ALSA `default` device (= PipeWire via `pipewire-alsa`), 16 kHz mono s16 | Simplest C API; PipeWire resamples; other programs can still use the Jabra |
| ONNX Runtime | Microsoft's prebuilt release in `/opt/onnxruntime`, not Debian's `libonnxruntime-dev` | Debian's 1.21 build prints ~570 "Schema error" lines on every start (known packaging bug) → journal spam on the microSD |
| One service | Wake word, VAD and STT all in `ears` | They share one audio stream; splitting would mean streaming raw audio over MQTT |

### MQTT contract (new)

| Topic | When | Payload |
|---|---|---|
| `buddy/ears/wake` | Wake word heard | `{"model":"hey_jarvis_v0.1","score":0.97}` |
| `buddy/ears/state` (retained) | State changes | `{"state":"idle"}` / `listening` / `transcribing` / `offline` |
| `buddy/ears/transcript` | 1.7 | see 1.7 guide |

## Steps

### 1. Pi setup: ONNX Runtime + models (PC terminal, repo root = `skeleton/`)

```bash
deploy/deploy.sh --setup
```

New in `setup-pi.sh`: installs `libasound2-dev`, downloads ONNX Runtime to `/opt/onnxruntime-1.30.0` (checksum-verified, symlinked as `/opt/onnxruntime`), and runs `deploy/fetch-models.sh` (≈ 6 MB of ONNX models + ≈ 140 MB `ggml-base.en.bin` for 1.7). The first build also downloads and compiles whisper.cpp: **expect 3–6 min** ⚠️. Expected ending:

```
  ✓ melspectrogram.onnx          1.1M
  ✓ embedding_model.onnx         1.3M
  ✓ hey_jarvis_v0.1.onnx         1.3M
  ✓ silero_vad.onnx              2.3M
  ✓ ggml-base.en.bin             142M
...
  buddy@ears       active
✓ deployed. Logs: deploy/logs.sh <service>
```

Then retire the 1.5 test services:

```bash
ssh buddy systemctl --user disable --now buddy@ping buddy@pong
```

In CLion: **File → Reload CMake Project**. `/opt/onnxruntime` now exists, so the Debug profile configures; `buddy-ears` appears as a run configuration.

### 2. Offline test with a recording (no MQTT involved)

Record yourself at ~1 m: say **"Hey Jarvis"**, pause ½ s, **"play Spotify"**, then stay silent 2 s:

```bash
ssh buddy
pw-record --rate 16000 --channels 1 --format s16 ~/hey.wav    # Ctrl+C when done
~/buddy/bin/buddy-ears --wav ~/hey.wav
```

Expected ⚠️ (timings vary):

```
[ears] loaded /home/dev/buddy/models/ggml-base.en.bin in 300 ms
  → buddy/ears/state {"state":"idle"}
[ears] listening for "hey_jarvis_v0.1"
[ears] 1.84 s wake word (score 0.93)
  → buddy/ears/wake {"model":"hey_jarvis_v0.1","score":0.93}
  → buddy/ears/state {"state":"listening"}
  ...
```

`--wav` mode prints instead of publishing, so it doesn't conflict with the running `buddy@ears`.

### 3. Live test

Two terminals on the PC:

```bash
deploy/logs.sh ears
ssh buddy "mosquitto_sub -v -t 'buddy/ears/#'"
```

Say "Hey Jarvis" from your chair. Expected in the log: `wake word (score 0.9…)`; on the bus: `buddy/ears/wake …` and `buddy/ears/state {"state":"listening"}`. If you then say nothing: `no command after wake word` after 4 s, back to `idle`.

### 4. Tune with debug scores (CLion inner loop)

1. Stop the deployed copy (same MQTT client id): `ssh buddy systemctl --user stop buddy@ears`
2. CLion → Run configuration `buddy-ears` → Environment variables: `BUDDY_EARS_DEBUG=1`
3. Run. Every score ≥ 0.1 is printed: `[ears] 12.4 s score 0.37`.
4. Say "Hey Jarvis" 10× at 1 m and 10× at 2–3 m, normal voice, facing away a few times. Note the peak scores.
5. Talk normally, play a podcast/YouTube near the Jabra for a few minutes. Note the highest score that did **not** come from "Hey Jarvis".
6. Pick a threshold between the two, e.g. real peaks ≥ 0.8, noise ≤ 0.3 → keep 0.5. Set it on the Pi in `~/buddy/config/buddy.env` (`BUDDY_WAKE_THRESHOLD=…`), then `ssh buddy systemctl --user restart buddy@ears`.
7. Stop CLion's run, `ssh buddy systemctl --user start buddy@ears`.

### 5. False-positive soak

Leave it running for an evening with normal life around it (talking, music from another device, TV). Next day:

```bash
ssh buddy 'journalctl --user-unit buddy@ears --since "-12h" | grep -c "wake word"'
```

Subtract the times you said it. Target ⚠️: ≤ 1 false trigger per few hours.

## Verify

- [ ] `buddy@ears` active after `deploy.sh`; `ping`/`pong` disabled.
- [ ] `--wav` test detects the wake word in your recording.
- [ ] Live: ≥ 9 of 10 "Hey Jarvis" from 2–3 m trigger.
- [ ] Soak: few false triggers; threshold noted in `buddy.env`.
- [ ] `ssh buddy sudo reboot` → after ~1 min, "Hey Jarvis" triggers without logging in.

**Done when:** triggers reliably from 2–3 m, few false positives.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| CMake: `Could NOT find ONNXRUNTIME_INCLUDE_DIR` | `/opt/onnxruntime` missing (CLion reloaded before setup) | Run step 1, then Reload CMake Project |
| `cannot load model …/melspectrogram.onnx` | Models not downloaded | `ssh buddy bash ~/buddy/src/deploy/fetch-models.sh` |
| `cannot open audio device "default": …`, retrying | PipeWire not running for `dev`, or `pipewire-alsa` missing | 1.4 step 3; `ssh buddy dpkg -l pipewire-alsa` |
| Opens fine, but scores are always 0 | Jabra muted (red LED) or wrong default source | 1.4 steps 4–5; test with `pw-record` |
| Triggers only very close | Threshold too high / Jabra input volume low | Debug scores (step 4); `wpctl set-volume @DEFAULT_AUDIO_SOURCE@ 1.0` |
| Triggers on TV / music | Threshold too low | Raise `BUDDY_WAKE_THRESHOLD` (0.6–0.7) or `BUDDY_WAKE_PATIENCE` (3) |
| Log flips connected / connection lost | CLion run + systemd unit at the same time | Stop one (step 4.1) |
| `start-limit-hit` | Crash loop, e.g. missing model | `deploy/logs.sh ears` shows why; fix, then `systemctl --user reset-failed buddy@ears` |
| `--wav`: `need 16 kHz mono 16-bit PCM` | File recorded with other settings | Re-record with the exact `pw-record` flags above |

## Optional: custom "Hey buddy" model

See `guides/01-06-custom-wake-word.md`. openWakeWord's own training notebook is broken on current Colab, so that guide uses a community notebook that patches it, and compares the result against Jarvis before you switch.

## Notes for later phases

- **2.x motion/eyes:** subscribe to `buddy/ears/state` (retained) and `buddy/ears/wake`. Eyes: `listening` = attentive, `transcribing` = thinking. Don't rename these topics.
- **1.11:** Spotify through the same Jabra will produce wake word scores from the music. Ducking on `buddy/ears/wake` + possibly PipeWire echo-cancel; ears will then capture from the echo-cancel source via `BUDDY_EARS_DEVICE`.
- **1.12:** ears already retries opening the audio device every 2 s; check after a cold boot that it doesn't need more (e.g. `After=pipewire.service`).
- **D3 offloading:** `buddy-ears` captures locally, so offloading ears means a microphone on the LAN host. Unlikely to be needed.
