# 1.10 – Voice replies (TTS)

> Written 2026-10-23. `buddy-voice` compiles (syntax check) against libpiper v1.8.0's real header; **the libpiper build and actual speech were not yet run**, neither in the sandbox nor on the Pi. Items marked ⚠️ are assumptions, especially all Pi 5 timings.

## Goal

Buddy talks back: "Playing Daft Punk.", "Sorry, I didn't catch that.", "Spotify isn't running on Buddy." Anything published on `buddy/voice/say` is spoken through the Jabra, mixed with the music.

**Done when:** Buddy answers commands audibly.

## Prerequisites

- 1.8 + 1.9 done: the brain and spotify services publish replies on `buddy/voice/say`.
- `deploy/deploy.sh --setup` run after 2026-10-23 (it builds libpiper and downloads the default voice; 1.8 step 1 already did this).

## Parts / tools

| Tool | Where | Role |
|---|---|---|
| libpiper v1.8.0 (Piper's C/C++ library, `OHF-Voice/piper1-gpl`) | Built by `setup-pi.sh` into `/opt/libpiper` | Text → audio, on the Pi's CPU via ONNX Runtime |
| espeak-ng (built inside libpiper) | `/opt/libpiper/share/espeak-ng-data` | Text → phonemes (how words are pronounced) |
| Piper voice `en_US-lessac-medium` | `~/buddy/models/*.onnx` + `.onnx.json` | The voice itself (~60 MB) |
| `buddy-voice` | Pi, `services/voice` | MQTT → libpiper → ALSA `default` (PipeWire) |

## Background

```
 brain / spotify ──buddy/voice/say {"text"}──► voice ──► Piper: text → phonemes → audio ──► ALSA default ──► PipeWire ──► Jabra
                                                 │                                            (mixed with librespot)
                                                 └─► buddy/voice/state {"state":"speaking"|"idle"}  (retained)
```

- **How Piper works:** espeak-ng turns text into phonemes (sounds), then a small neural network (the voice, an ONNX model) turns phonemes into audio. It runs well on a Pi's CPU: much faster than real time ⚠️.
- **libpiper vs the `piper` program:** Piper's maintained version (`piper1-gpl`) is mostly a Python package, but it also ships `libpiper`, a C library with a C++ core. That's what `buddy-voice` links (D5: no Python). It isn't packaged by Debian, so `setup-pi.sh` builds it once, pinned to v1.8.0, against the same ONNX Runtime ears uses. The build also clones and compiles espeak-ng (and a Japanese text frontend it always includes), which is why it takes a while.
- **Streaming by sentence:** Piper produces audio sentence by sentence. `buddy-voice` plays the first sentence while computing the next, so the time until you hear something ≈ synthesis time of the first sentence.
- **Mixing:** voice opens the ALSA `default` device per reply; PipeWire mixes it over the music and resamples 22 050 Hz → the Jabra's rate. Music isn't lowered yet: that's 1.11.
- **Voices:** quality levels `x_low`/`low` (16 kHz, faster) and `medium`/`high` (22 kHz, nicer). Listen first at <https://rhasspy.github.io/piper-samples/>. High German: `de_DE-thorsten-medium`. There's no Swiss German voice ⚠️.
- **License:** `piper1-gpl` is GPL-3.0. Linking it makes `buddy-voice` GPL if you ever *distribute* it. Running it privately doesn't matter.

### Who says what

| Situation | Sender | Text |
|---|---|---|
| Transcript not understood | brain | "Sorry, I didn't catch that." |
| "play <query>" found something | spotify | "Playing Daft Punk." / "Playing Around the World by Daft Punk." / "Playing the playlist …" |
| Search found nothing | spotify | "I couldn't find … on Spotify." |
| Spotify errors | spotify | "Spotify isn't running on Buddy.", "There's nothing to play on Buddy.", "I can't reach Spotify.", "Spotify isn't logged in.", … |
| play / pause / next / volume ok | — | silent: the music is the answer |

### MQTT contract

```
buddy/voice/say    {"text":"Playing Daft Punk."}     command, QoS 1
buddy/voice/state  {"state":"speaking"}              event, retained: idle | speaking | offline
```

Replies are queued and spoken one after another. Texts longer than 400 characters are cut.

### Decisions made in this step

| Topic | Choice | Why |
|---|---|---|
| TTS library | libpiper v1.8.0, built from source into `/opt/libpiper` | C API, no Python; not in Debian; same ONNX Runtime as ears |
| Default voice | `en_US-lessac-medium` | Clear, common, 22 kHz; swap via `BUDDY_VOICE_MODEL` |
| Playback | ALSA `default` opened per reply | Same route as ears; PipeWire mixes with Spotify; Jabra sink can suspend when idle |

## Steps

### 1. Check libpiper and the voice

```bash
ssh buddy
ls /opt/libpiper/lib/libpiper.so /opt/libpiper/share/espeak-ng-data/phontab
ls -lh ~/buddy/models/en_US-lessac-medium.onnx*
```

Expected: both libpiper files exist; the voice `.onnx` (~61 MB) and `.onnx.json` (~5 KB). If not: `bash ~/buddy/src/deploy/setup-pi.sh` (libpiper) or `bash ~/buddy/src/deploy/fetch-models.sh en_US-lessac-medium` (voice).

### 2. Offline test: write a WAV

```bash
~/buddy/bin/buddy-voice --say "Hello, I am Buddy. Playing Daft Punk." --wav ~/hello.wav
pw-play ~/hello.wav
```

Expected ⚠️:

```
[voice] loaded /home/dev/buddy/models/en_US-lessac-medium.onnx in 350 ms
[voice] "Hello, I am Buddy. Playing Daft Punk." (2900 ms audio, first audio after 180 ms, done after 450 ms)
```

"done after" far below "ms audio" = faster than real time. You should hear a clear voice from the Jabra.

### 3. Speak directly

```bash
~/buddy/bin/buddy-voice --say "Testing, one, two, three."
```

Expected: you hear it, no WAV in between. If the first syllable is cut off, see Troubleshooting.

### 4. Pick a voice (optional)

```bash
bash ~/buddy/src/deploy/fetch-models.sh en_GB-alan-medium en_US-ryan-medium
for v in en_US-lessac-medium en_GB-alan-medium en_US-ryan-medium; do
    echo "== $v"; BUDDY_VOICE_MODEL=$v.onnx ~/buddy/bin/buddy-voice --say "Playing Daft Punk. Sorry, I didn't catch that."
done
```

Write your choice into `~/buddy/config/buddy.env`:

```bash
BUDDY_VOICE_MODEL=en_US-lessac-medium.onnx
```

Speed and loudness, same file: `BUDDY_VOICE_LENGTH_SCALE=1.1` (slower), `BUDDY_VOICE_GAIN=0.8` (quieter than the music).

### 5. Via MQTT

```bash
ssh buddy systemctl --user restart buddy@voice
ssh buddy "mosquitto_sub -v -t 'buddy/voice/#'" &
ssh buddy "mosquitto_pub -q 1 -t buddy/voice/say -m '{\"text\":\"Testing over MQTT.\"}'"
```

Expected: Buddy says it, and:

```
buddy/voice/state {"state":"idle"}
buddy/voice/say {"text":"Testing over MQTT."}
buddy/voice/state {"state":"speaking"}
buddy/voice/state {"state":"idle"}
```

Stop the background subscriber with `kill %1`.

### 6. End to end

With music paused:

- "Hey Jarvis, play Daft Punk" → "Playing Daft Punk." then the music.
- "Hey Jarvis, what's the weather" → "Sorry, I didn't catch that."
- `ssh buddy systemctl --user stop buddy-librespot`, then "Hey Jarvis, play Spotify" → "Spotify isn't running on Buddy." Then `systemctl --user start buddy-librespot` again.

With music playing: "Hey Jarvis, play Queen" → the reply is audible over the music (it may be hard to hear at high volume; 1.11 fixes that).

Timing: `deploy/logs.sh voice` shows "first audio after … ms" per reply.

## Verify

- [ ] `--say … --wav` produces a clear WAV (step 2).
- [ ] `--say` plays through the Jabra (step 3).
- [ ] `mosquitto_pub` on `buddy/voice/say` is spoken; `buddy/voice/state` goes speaking → idle (step 5).
- [ ] "play <artist>" is confirmed audibly; nonsense gets "Sorry, …"; a Spotify failure is spoken (step 6).
- [ ] First audio ≤ ~0.5 s after the message ⚠️.

**Done when:** Buddy answers commands audibly.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| setup: `imported target "onnxruntime::onnxruntime" references … lib64/libonnxruntime.so…` | libpiper picked up ONNX Runtime's broken CMake config (setup-pi.sh before 2026-10-23 fix) | `git pull`, run `deploy/deploy.sh --setup` again |
| setup: libpiper build fails cloning espeak-ng | No network to GitHub, or `git` missing | `git --version`; retry `setup-pi.sh` |
| setup: libpiper build killed / Pi freezes | Out of memory while compiling | Close CLion's remote build; retry. ⚠️ Shouldn't happen on 8 GB |
| `voice model not found` | Model not downloaded / name typo | `fetch-models.sh <voice>`; `BUDDY_VOICE_MODEL` needs the `.onnx` ending |
| `voice config not found … .onnx.json` | JSON missing next to the model | `fetch-models.sh <voice>` downloads both |
| `espeak-ng data not found` | libpiper installed elsewhere | `ls /opt/libpiper/share`; set `BUDDY_VOICE_ESPEAK_DATA` |
| `error while loading shared libraries: libpiper.so` | Binary built before libpiper existed | `deploy/deploy.sh voice` (rebuild) |
| `cannot open audio device "default"` | PipeWire not running for this user | 1.4 step 3; `systemctl --user status pipewire` |
| First syllable missing | Jabra sink waking from suspend takes longer than the 100 ms lead-in | Raise the lead silence in `audio_sink.cpp` (`AlsaSink::begin`) to 200–300 ms |
| Robotic / wrong pronunciation of names | espeak-ng phonemes | Write the reply phonetically, or another voice |
| Voice too quiet over music | No ducking yet | `BUDDY_VOICE_GAIN` (≤ ~1.5, clipping above), or wait for 1.11 |
| Buddy hears itself and triggers | Reply contains something like the wake word | Unlikely with the current texts; 1.11 makes ears ignore wake words while `voice/state` = speaking |

## Notes for later phases

- **1.11 ducking & AEC:** `buddy/voice/state` (retained) tells ears and spotify when Buddy speaks. Plan: lower the music on `ears/wake` and while `voice/state` = speaking; ears ignores wake words while speaking unless the Jabra's own echo cancellation (1.4 step 8) is good enough.
- **1.12:** voice opens the audio device per reply, so a Jabra that appears late after boot is no problem. Check that the model load time (~0.3 s ⚠️) is fine at boot.
- **2.4 eyes:** `voice/state` = speaking → mouth/eye animation. For lip sync, libpiper returns phoneme timings per chunk (`piper_audio_chunk.phonemes` / `alignments`). Not used yet.
- **German:** `fetch-models.sh de_DE-thorsten-medium`, `BUDDY_VOICE_MODEL=de_DE-thorsten-medium.onnx`, and translate the reply texts in `services/brain/main.cpp` and `services/spotify/player.cpp`.
