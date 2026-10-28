# Phase 1 summary: voice assistant (done)

Condensed from the step guides 1.2–1.12 (full versions in Git history: `git log --all -- 'guides/*' 'docs/guides/*'`).
Everything needed to rebuild the Pi, re-tune or debug is here; walkthrough text and one-time checks are gone.
Speech-to-text training is a recurring job with its own doc: `docs/voice-training.md`.

Result: "Hey Jarvis, play Daft Punk" → the music box plays it and Buddy confirms. While Buddy listens or speaks, the box
is turned down. Starts on power-on, recovers from crashes, hangs and audio hiccups by itself.

```
 Jabra mic ─► echo-cancel ─► ears ──transcript──► brain ──cmd──► spotify ──Web API──► Spotify ──► music box (Connect)
 (16 kHz)     (Buddy's voice   wake word,          rules           │  ▲                            ▲ volume down
               removed)        VAD, whisper                         │  └─ ears/voice state ─────────┘ while busy (ducking)
                                                                    └─► voice/say ──► voice (Piper) ──► Jabra speaker
```

## Rebuild from scratch

| # | Step | Details |
|---|---|---|
| 1 | Hardware | 1.2 below |
| 2 | Flash + first boot | 1.3 below; PC `~/.ssh/config` alias `buddy` |
| 3 | `deploy/deploy.sh --setup` (PC, repo root `skeleton/`) | apt packages, ONNX Runtime, libpiper, uv, PipeWire config, Mosquitto, lingering, `buddy.env`, models; then builds and starts all services |
| 4 | `~/buddy/config/buddy.env` | `BUDDY_SPOTIFY_CLIENT_ID`, `BUDDY_SPOTIFY_DEVICE` (box name), `BUDDY_SPOTIFY_DEFAULT_URI`, your voice/STT settings |
| 5 | Own models (PC → Pi) | `scp ggml-buddy-*.bin buddy:buddy/models/` (and a custom wake word if any); `fetch-models.sh` names what's missing |
| 6 | Spotify login | `buddy-spotify --login` (1.8/1.9) |
| 6b | Hue pairing | Bridge's link button, then `buddy-hue --pair` (1.13); `BUDDY_HUE_DEFAULT_ROOM` |
| 7 | Check | `deploy/status.sh`, then "Hey Jarvis, play Spotify", "… lights off"; panel `http://buddy.local:8080` |

`setup.sh <step>` runs single steps (`setup.sh audio`); `setup.sh check` warns about unknown keys in `buddy.env`.

## 1.2 Assemble the Pi

- **Active Cooler is permanent**: push pins + thermal pads degrade on removal. Fan cable in the 4-pin **FAN** header
  (remove its plastic cap first), clear of the blades. Board supported from below while pressing the pins.
- **Jabra in a black USB 2 port** (blue USB 3 stays free). USB only, no Bluetooth.
- **27 W supply** (5.1 V / 5 A) unlocks the 1.6 A USB budget (else 600 mA). Check: `od -An -tu4 --endian=big /proc/device-tree/chosen/power/max_current` → `5000`.
- **GPIO header stays free**: motion goes through the Pico over USB (2.2). Bare board: no metal near the GPIO pins (5 V next to GND).
- **Shutdown rule**: `sudo poweroff`, wait for the green LED to stop, then unplug. Pulling power while the OS runs risks the microSD.
- Later: 2.6 housing needs an intake above the blower and a vent at its side exhaust, PETG HF near the Pi; keep the Jabra
  mic away from the exhaust. Camera (4.2) uses a CAM/DISP flat-cable connector.

## 1.3 OS setup

| Imager setting | Value |
|---|---|
| Device / OS | Raspberry Pi 5 / Raspberry Pi OS Lite (64-bit) = Debian 13 trixie |
| Hostname / user | `buddy` / `dev` (used in units and paths) |
| Localisation | Bern, Europe/Zurich, keyboard `ch` (also sets the Wi-Fi country) |
| SSH | Public key only; Raspberry Pi Connect off |

PC `~/.ssh/config`: `Host buddy` → `HostName buddy.local`, `User dev`, `IdentityFile ~/.ssh/<your key>`.

After first boot: `sudo apt update && sudo apt full-upgrade -y`, `sudo rpi-eeprom-update -a`, reboot.
Password SSH off: `echo 'PasswordAuthentication no' | sudo tee /etc/ssh/sshd_config.d/10-no-password.conf && sudo systemctl reload ssh`.

Health: `vcgencmd get_throttled` must be `0x0` (never undervolted/throttled since boot); under
`stress-ng --cpu 4 --timeout 120s` the temperature levels off at ~60–70 °C. `deploy/status.sh` shows both.

| Symptom | Fix |
|---|---|
| `buddy.local` doesn't resolve | Same non-guest network? Use the IP from the router meanwhile |
| `REMOTE HOST IDENTIFICATION HAS CHANGED` after reflashing | `ssh-keygen -R buddy.local` |
| `throttled` ≠ `0x0` | 27 W supply directly, no adapters; re-seat the cooler |
| No Wi-Fi | Ethernet, then `nmcli dev wifi list` / `nmcli dev status` |

## 1.4 Audio

- **PipeWire runs per user**, so the `dev` session must exist without a login: **lingering** (`setup.sh session`).
  Packages: `pipewire pipewire-alsa pipewire-pulse wireplumber alsa-utils`;
  `systemctl --user enable --now pipewire.socket pipewire-pulse.socket wireplumber.service`.
- Jabra must be the default **sink and source** (`*` in `wpctl status`); set with `wpctl set-default <id>`, WirePlumber
  remembers it by device name. Volumes: sink 0.6, source 1.0 unmuted. The Jabra mic is 16 kHz only (cuts off at 7 kHz).
- Speech format everywhere: **16 kHz, mono, s16**. Test: `pw-record --rate 16000 --channels 1 --format s16 x.wav` / `pw-play x.wav`.
  Programs use ALSA `default` or `pipewire:NODE=<name>` (via pipewire-alsa), never `hw:`.

| Symptom | Fix |
|---|---|
| Jabra missing in `/proc/asound/cards` | Other black USB port; `lsusb`; `dmesg \| tail` after plugging in |
| `cannot set freq 48000 to ep 0x3` in dmesg | Known Jabra 510 quirk: ignore if audio works |
| `Could not connect to PipeWire` / works only when logged in | Lingering off (`loginctl show-user dev -p Linger`) or user services not enabled |
| Jabra in cards but not in `wpctl status` | `systemctl --user restart wireplumber` |
| Silent recording | Jabra mute LED red, source muted, wrong default source |
| `arecord -D hw:…` busy | Expected (PipeWire owns it): use `-D default` |
| Crackling | `vcgencmd get_throttled`; other USB port |

## 1.5 Skeleton, MQTT & workflow

Decisions live in CLAUDE.md (Code conventions, D2, D5–D7, D15). Repo root for all commands: `skeleton/`.
Services are C++ (CMake) or Python (uv workspace); both run as `buddy@<name>` and follow the same pattern
(`skeleton/CLAUDE.md`).

| Pi path | Content |
|---|---|
| `~/buddy/dev/` | CLion's synced copy + Debug build |
| `~/buddy/src/`, `~/buddy/build-release/` | `deploy.sh`'s copy and Release build (ccache) |
| `~/buddy/bin/` | Installed binaries; Python services as links into `~/buddy/venv` |
| `~/buddy/models/` | Models (not in Git) |
| `~/buddy/config/buddy.env`, `spotify-token.json` | Runtime config + secrets, mode 600, not in Git |
| `~/buddy/voice/` | Recording sessions until `deploy/train.sh pull` copies them to the PC |
| `~/.config/systemd/user/buddy@.service` | The one unit template |
| `~/.local/bin/uv` | uv for Python services (`setup.sh uv`, pinned) |

**CLion Full Remote Mode** (Dev's PC):
1. Toolchains → `+` Remote Host `buddy-pi`: host `buddy.local`, user `dev`, auth via OpenSSH config/agent; CMake, GCC, GDB get green checks.
2. CMake Debug profile: toolchain `buddy-pi`, generator Ninja, options `-DCMAKE_CXX_COMPILER_LAUNCHER=ccache`.
3. Deployment → SFTP entry → Mappings → deployment path **`/home/dev/buddy/dev`** (default is under `/tmp` = RAM disk, wiped on reboot).
4. Before Run/Debug: `ssh buddy systemctl --user stop buddy@<name>` (same MQTT client id; otherwise both flip connected/lost).

Tests: `deploy/test.sh` (C++ GoogleTest under sanitizers, pytest, shellcheck; layout in `skeleton/CLAUDE.md`),
`deploy/test.sh --pi` for the C++ tests on the Pi. Bus: `mosquitto_sub -v -t 'buddy/#'`.

| Symptom | Fix |
|---|---|
| CMake can't find `PahoMqttCpp` | `deploy.sh --setup` (apt `libpaho-mqttpp-dev`) |
| Log flips `connected` / `connection lost` | Two copies of one service (CLion + unit): stop one |
| `Connection refused` | `sudo systemctl status mosquitto` |
| `Failed to connect to bus` from `systemctl --user` | Lingering off |
| `deploy/logs.sh` empty / permission error | `sudo usermod -aG systemd-journal dev`, log in again |
| CLion sync slow | No local rsync (Windows): falls back to SFTP; acceptable or rsync via WSL |
| Topic constant missing in C++ | `shared/topics.json` is the source; `topics.hpp` is generated (Reload CMake Project) |

**Offloading ears/brain to a LAN PC (D3), not done:** Mosquitto needs a LAN listener with a password, e.g.
`/etc/mosquitto/conf.d/buddy.conf` with `listener 1883`, `allow_anonymous false`, `password_file /etc/mosquitto/passwd`;
the PC runs the service in Docker with `BUDDY_MQTT_HOST=buddy.local` + user/password.

## 1.6 Wake word

openWakeWord in C++ (`services/ears/wake_word.*`), every 80 ms chunk:

```
 1280 samples ─► melspectrogram.onnx ─► 8 mel frames      (same for every wake word)
 last 76 frames ─► embedding_model.onnx ─► 96 features      (same for every wake word; ~3.7 of 4.2 ms per chunk on the Pi)
 last 16 features ─► hey_jarvis_v0.1.onnx ─► score 0..1     (the only wake-word-specific file)
```

Trigger: score ≥ `BUDDY_WAKE_THRESHOLD` (0.5) for `BUDDY_WAKE_PATIENCE` (2) chunks in a row. Threshold up = fewer false
triggers, clearer speech needed. ONNX Runtime 1.30.0 is Microsoft's prebuilt release in `/opt/onnxruntime` (D9).
ears ignores the wake word while Buddy speaks (`BUDDY_EARS_IGNORE_WHILE_SPEAKING=1`). ears idles at ~8 % of one core.
MQTT: `buddy/ears/wake {"model","score"}`, `buddy/ears/state` retained `idle|listening|transcribing|offline`.

**Offline test** (no MQTT, doesn't disturb the running unit): record "Hey Jarvis", pause, "play Spotify", 2 s silence
with `pw-record …` (1.4 format), then `~/buddy/bin/buddy-ears --wav ~/hey.wav`.

**Tuning** (needs your voice, with the box playing): stop `buddy@ears`, run it with `BUDDY_EARS_DEBUG=1` (prints every
score ≥ 0.1). Say the wake word 10× at 1 m and 10× at 2–3 m, then play TV/podcast for a few minutes. Pick a threshold
between the real peaks and the highest noise score; set it in `buddy.env`. Soak overnight, then count:
`journalctl --user-unit buddy@ears --since "-12h" | grep -c "wake word"` (target ≤ 1 false trigger per few hours).
`deploy/record.sh` also records 20× "Hey Jarvis" per session: material for offline threshold checks.

| Symptom | Fix |
|---|---|
| CMake: `Could NOT find ONNXRUNTIME_INCLUDE_DIR` | `/opt/onnxruntime` missing: `deploy.sh --setup`, then Reload CMake Project |
| `cannot load model …` | `bash ~/buddy/src/deploy/pi/fetch-models.sh` |
| Scores always 0 | Jabra muted or wrong source |
| Triggers only close by | Threshold lower; source volume 1.0 |
| Triggers on TV/music | Threshold 0.6–0.7 or patience 3 |
| `--wav`: `need 16 kHz mono 16-bit PCM` | Re-record with the exact `pw-record` flags |

### Custom "Hey buddy" model (optional, not done yet)

Trained once in Google Colab with the community notebook
<https://colab.research.google.com/github/alfiedennen/openwakeword-colab-2026/blob/main/train_wakeword.ipynb>
(openWakeWord's own notebook has been broken on current Colab since late 2025, issue #296).
`buddy-ears` loads the result unchanged via `BUDDY_WAKE_MODEL`.

1. **File → Save a copy in Drive** (pip packages are unpinned; your copy survives upstream changes).
2. Runtime: **L4 + High-RAM** (Colab Pro, 75–90 min) or free T4 (~2.5 h, tab in the foreground). A disconnect = empty VM, ~25 GB downloads again; peak disk ~35 GB.
3. Config cell "★ EDIT THESE TWO LINES ★": `TARGET_PHRASE = ['hey buddy']`, `MODEL_NAME = 'hey_buddy'`, and
   `'custom_negative_phrases': ['buddy', 'hey', 'hey body', 'hey baby', 'hey daddy', 'hey bud', 'hey muddy', 'study', 'nobody', 'somebody', 'everybody', 'anybody', 'hey buddy holly', 'a buddy', 'my buddy']`
   (drop the last three if "…my buddy" should trigger). Other values default for run 1.
4. **Run all**. After section 11, listen to 5 random clips in `/content/hey_buddy_output/hey_buddy/positive_train/*.wav`;
   odd pronunciation → add a phonetic variant to `TARGET_PHRASE`.
5. Section 15 must print input `[batch, 16, 96]`, output `[batch, 1]`. Validation recall 0.5–0.7 is normal; `fp/hr` > 1 → expect TV triggers.
   **Back up `hey_buddy.onnx`** (not in Git, can't be re-downloaded).
6. `scp hey_buddy.onnx buddy:buddy/models/`; offline: `BUDDY_WAKE_MODEL=hey_buddy.onnx BUDDY_EARS_DEBUG=1 ~/buddy/bin/buddy-ears --wav ~/hey-buddy.wav`
   (and must *not* trigger on the Jarvis recording).
7. Compare with Jarvis using the tuning procedure above (hits at 1 m and 2–3 m, highest TV and box-music score, soak).
   Switch only if at least as good on hits and clearly better on false triggers: `BUDDY_WAKE_MODEL=hey_buddy.onnx`.
8. Next runs, one change each, renamed `hey_buddy_v2.onnx` …: misses from afar → `n_samples: 5000`, `augmentation_rounds: 2`;
   TV triggers → `max_negative_weight: 3000`; specific false trigger → add it to the negatives.

The training could move into `tools/` now that Python is allowed (D15), like speech-to-text. Eyes/head (2.x) react to
`buddy/ears/wake` regardless of `model`.

## 1.7 Speech-to-text

After the wake word, ears records until you stop talking, transcribes on the Pi's CPU and publishes the text.

```
 wake ─► record ──► VAD: speech ──► VAD: 700 ms silence ──► whisper ──► buddy/ears/transcript
            └─ nothing said for 4 s → give up    └─ or 5 s max
```

- **Silero VAD v6.2.3** (32 ms frames) finds the end of the command; `BUDDY_VAD_SILENCE_MS` (700) adds directly to the
  latency. A **level gate** ignores background speech quieter than you: the wake word shows your level, the 2 s before it
  the background's (`BUDDY_VAD_LEVEL_GATE`, 0 = off).
- **whisper.cpp v1.9.5**, built static by CMake (`third_party/`, `GGML_NATIVE`, `-O2` even in Debug). Greedy, no
  timestamps, single segment, non-speech tokens suppressed, commands < 1 s padded to 1.1 s.
- **Model:** `ggml-buddy-base.en-*` (fine-tuned on your voice, `docs/voice-training.md`); stock `ggml-base.en.bin` is the
  fallback. Measured on the Pi (57 held-out clips, trimmed, `deploy/train.sh eval`, 2026-10-25; tiny/small from an
  earlier run on untrimmed clips, without intent scoring):

| Model | Exact | Intent | WER | STT time |
|---|---|---|---|---|
| tiny.en | 19/57 | — | 38.7 % | 0.79 s |
| base.en | 23/57 | 16/43 | 27.5 % | 1.75 s |
| small.en | 26/57 | — | 24.8 % | 6.20 s (too slow) |
| buddy-base.en (first fine-tune) | 40/57 | 28/43 | 11.7 % | 1.75 s |
| buddy-base.en, `audio_ctx=auto` | 35/57 | 26/43 | 14.3 % | **0.20 s** |
| base.en, `audio_ctx=auto` | 18/57 | 16/43 | 36.2 % | 2.45 s (p90 11 s) |

- **`BUDDY_STT_AUDIO_CTX`**: whisper always encodes a 30 s window unless told otherwise. `auto` sizes it to the command
  (50 frames per second + 16): ~9× faster. Stock models fall apart with it (hallucination loops); models trained with
  `deploy/train.sh` (default `--ctx auto`) are made for it, and `train.sh install` sets it with the model.
- **`BUDDY_STT_FALLBACK`** (default 0): whisper's re-decoding at higher temperature. Same accuracy on commands, but
  multi-second worst cases.
- `BUDDY_STT_PROMPT` (vocabulary hint) and the language: compare with `deploy/train.sh eval --prompt …`. `.en` models are
  English only; German needs a multilingual model + `BUDDY_STT_LANGUAGE=de` (never `auto` on 1-second commands).
- **Data from real use:** `BUDDY_EARS_SAVE_DIR=~/buddy/voice-live` keeps every command's audio + transcript (newest
  `BUDDY_EARS_SAVE_MAX`, ~100 KB each) for `deploy/train.sh label`.

MQTT: `buddy/ears/transcript {"text":"Play Spotify.","language":"en","audio_ms":2160,"stt_ms":200}` (text verbatim
from whisper; normalising is brain's job).

Offline: `buddy-ears --wav f.wav` (whole pipeline), `buddy-ears --transcribe a.wav b.wav` (speech-to-text only, one
`file⇥ms⇥text` line each; clips must be trimmed like live commands, which `deploy/train.sh eval` does).

| Symptom | Fix |
|---|---|
| `cannot load whisper model …` | `ls ~/buddy/models`; `fetch-models.sh` (own models: copy from the PC) |
| Transcript empty or "you" / "Thank you." | whisper on silence/noise: VAD ended late or speech too quiet; check levels, `BUDDY_VAD_THRESHOLD` |
| Commands cut mid-sentence / feels sluggish | `BUDDY_VAD_SILENCE_MS` 900 / 500 |
| Fan or music keeps it listening to 5 s | `BUDDY_VAD_THRESHOLD=0.6`; is the box ducking? |
| First word missing ("lay Spotify") | Pause after "Hey Jarvis", or `BUDDY_WAKE_PATIENCE=1` |
| Garbage with `BUDDY_STT_AUDIO_CTX=auto` | Stock model: set `0`, or install a model trained with `--ctx auto` |
| `stt` slow or slower over minutes | Model size; `vcgencmd get_throttled` (cooling) |
| Wake word missed right after a command | Detector warms up 1.3 s after each command: expected |

## 1.8 / 1.9 Brain & Spotify

```
 ears ──transcript──► brain ──buddy/spotify/cmd──► spotify ──HTTPS──► Spotify Web API ──► music box (Connect device)
                        └─► voice/say "Sorry, …"      └─► voice/say "Playing …"
```

- **brain**: lowercases, strips punctuation and politeness, turns number words into digits, fixes whisper's "Spotify"
  spellings and "play mode playlist", then regex rules that must match the **whole** sentence (behind `IntentParser`,
  LLM-replaceable, D11). `tools/voice/commands.tsv` lists every phrasing it must understand; a test checks each of them (`deploy/test.sh -R Corpus`).
  A new phrasing goes into the corpus first, then into `services/brain/rules.cpp`.
- **spotify**: the Web API is the remote control; the **music box** (`BUDDY_SPOTIFY_DEVICE`, its name in the Spotify app)
  is the speaker (D10). OAuth with PKCE: no client secret on the Pi, refresh token in `~/buddy/config/spotify-token.json`.
  Every command looks the box up by name (`GET /me/player/devices`); on the wake word that lookup happens ahead of time.
- **Who talks:** whoever knows the outcome. play/pause/next/volume stay silent: the music is the answer.

| You say (examples) | Intent → what spotify does |
|---|---|
| "play Spotify", "keep playing", "resume" | `play` → resume on the box (transfer if another device is active; nothing to resume → `BUDDY_SPOTIFY_DEFAULT_URI`) |
| "play Daft Punk", "put on City Lights from James Gruntz", "play some Strokes" | `play` + query → your own playlist of that name, else search: exact artist (ignoring "the"/"some") → artist; "<song> by <artist>" with field filters; best track |
| "play my playlist Chill", "play the Focus playlist", "play my playlist" | `play_playlist` → your/followed playlists, fuzzy (a word 1 letter off counts); none matches → public playlists; no name → default URI |
| "play / shuffle my liked songs" | `play_liked` → whole library (undocumented context ⚠️), else the 50 newest liked songs |
| "pause", "stop the music" | `pause` |
| "next track", "skip this one" / "go back one song" | `next` / `previous` |
| "a bit louder", "turn it down a little" | `volume_up` / `volume_down` (±`BUDDY_SPOTIFY_VOLUME_STEP`, 10 %) |
| "set the volume to 50%" | `set_volume` |
| "shuffle on", "stop shuffling" / "repeat this song", "repeat off" | `shuffle`, `repeat` (spoken confirmation) |
| "what's playing?" | `now_playing` → "This is <title> by <artist>." |
| "play Daft Punk on the bathroom speaker", "my liked songs on desktop one", "move the music to my Pixel" | the same on that speaker; no query = move what's playing there. Name: exact, contained ("pixel" → Pixel 8) or a word 1–2 letters off; "the box" = the box; two matches → "Which speaker?"; none → "I can't find a speaker called …" (or, for "Shine On You Crazy Diamond", the search brain's `fallback` names) |

Which speaker: a named one; else `play`/`play_playlist`/`play_liked` → the box, everything else (pause, next, volume,
shuffle, repeat) → the device playing right now (else the box). Phones and PCs are only in the list while their
Spotify app is open.

Setup (once): developer.spotify.com → **Create app** (Redirect URI exactly `http://127.0.0.1:8888/callback`, Web API),
Client ID into `BUDDY_SPOTIFY_CLIENT_ID`; owner needs **Premium** (Development Mode, max 5 users, search max 10 results).
Login: load `buddy.env` (`while IFS="=" read -r k v; do [[ "$k" =~ ^BUDDY_ ]] && export "$k=${v%$'\r'}"; done < ~/buddy/config/buddy.env`),
`~/buddy/bin/buddy-spotify --login`, open the URL on the PC, copy the failing `http://127.0.0.1:8888/callback?code=…` URL
back. A new scope (e.g. liked songs) needs `--login` again.

Offline: `buddy-brain --test "Play Daft Punk." …`; `buddy-spotify --cmd <action> [query] [speaker]` (real API, no MQTT;
plays on the box unless a speaker is named), `--cmd devices` (exact device names), `--cmd duck` / `restore`.

| Symptom | Fix |
|---|---|
| `BUDDY_SPOTIFY_DEVICE is not set` / `device "…" not found` | `buddy-spotify --cmd devices` with the box awake; name must match (case doesn't matter). In standby some boxes leave the list ⚠️ (spotify then tries the last id it saw) |
| `BUDDY_SPOTIFY_CLIENT_ID is not set` | Manual run without loading `buddy.env` |
| Login: `INVALID_CLIENT: Invalid client` | Wrong value (secret instead of ID, hidden characters): `grep SPOTIFY_CLIENT_ID ~/buddy/config/buddy.env \| cat -A` must show 32 × `0-9a-f` then `$` |
| Login: `Invalid redirect URI` | Exactly `http://127.0.0.1:8888/callback` in both places |
| `state mismatch` | Pasted an older login's URL: `--login` again |
| "I'm not allowed to read your playlists / liked songs yet" | Token from before that scope: `--login` again |
| `token request rejected … invalid_grant` | Refresh token revoked: `--login` again |
| `HTTP 404 NO_ACTIVE_DEVICE` / "There's nothing to play" | Nothing to resume: set `BUDDY_SPOTIFY_DEFAULT_URI` or start something once from the app |
| `PREMIUM_REQUIRED` / `HTTP 403` everywhere | Premium on the app owner; account in the app's User Management |
| Wrong intent | Add the sentence to `commands.tsv`, run `deploy/test.sh -R Corpus`, fix `rules.cpp` |
| Names misheard ("James Crops") | Train STT with that name (`docs/voice-training.md`), or `BUDDY_STT_PROMPT` |

## 1.10 Voice

- **libpiper v1.8.0** (Piper's C/C++ library, `OHF-Voice/piper1-gpl`), built by `setup.sh libpiper` into `/opt/libpiper`
  against the same ONNX Runtime; espeak-ng (phonemes) is compiled inside. Patched to read its thread count from
  `PIPER_INTRA_OP_THREADS` (`BUDDY_VOICE_THREADS=3`: first audio ~830 → ~450 ms). GPL-3.0: matters only if distributed.
- Voice **`en_GB-alan-medium`** (22 kHz) with `BUDDY_VOICE_LENGTH_SCALE=0.9`, `BUDDY_VOICE_GAIN=1.05`. Samples:
  <https://rhasspy.github.io/piper-samples/>; more via `fetch-models.sh <voice>`. German: `de_DE-thorsten-medium`
  (no Swiss German voice) + translated reply texts in brain/spotify.
- Plays sentence by sentence (first sentence while the next is synthesised) through ALSA `default` → PipeWire → Jabra,
  opened per reply. Replies queue; > 400 characters are cut; replies older than 10 s are dropped.
- MQTT: `buddy/voice/say {"text"}` (command, QoS 1), `buddy/voice/state` retained `idle|speaking|offline`.
- Offline: `buddy-voice --say "Hello." [--wav out.wav]`.

| Symptom | Fix |
|---|---|
| `voice model not found` / `voice config not found … .onnx.json` | `fetch-models.sh <voice>` (fetches both; `BUDDY_VOICE_MODEL` needs `.onnx`) |
| `espeak-ng data not found` | `ls /opt/libpiper/share`; `BUDDY_VOICE_ESPEAK_DATA` |
| `error while loading shared libraries: libpiper.so` / `libonnxruntime.so.1` | `setup.sh libpiper` (sets libpiper's RUNPATH), then `deploy.sh voice` |
| setup: libpiper build fails cloning espeak-ng | Network/`git`; retry |
| First syllable missing | Lead-in silence in `AlsaSink::begin` (`audio_sink.cpp`) to 200–300 ms |
| Wrong pronunciation of names | Write the reply phonetically, or another voice |

## 1.11 Background music & echo cancellation

Music plays on the **music box**, not the Jabra, so it reaches the mic as room noise that Buddy can't subtract (D13).

- **Ducking** (spotify service): on `ears/state` listening/transcribing or `voice/state` speaking, the box goes to
  `BUDDY_SPOTIFY_DUCK_PERCENT` (30 %) of its volume via the Web API (~0.3 s, while you breathe in after "Hey Jarvis");
  `BUDDY_SPOTIFY_DUCK_HOLD_MS` (1500) after both are idle it comes back. "louder" while ducked changes the volume that
  comes back; a volume changed on the phone meanwhile is left alone. A crash while ducked is repaired at the next start
  (`$XDG_RUNTIME_DIR/buddy-spotify-duck.json`). `buddy/spotify/state` carries `ducked`. A box that refuses remote volume
  (`VOLUME_CONTROL_DISALLOW`) turns ducking off with a log line. Other speakers in the room: `BUDDY_SPOTIFY_DUCK_DEVICES`
  (the one playing is ducked, and restored by its device id).
- **The wake word itself** must get through the music unducked: threshold tuning with the box playing (1.6), training data
  recorded with the box playing (`deploy/record.sh box`), maybe "Hey buddy" trained with music.
- **Echo cancellation** (PipeWire `libpipewire-module-echo-cancel`, WebRTC, monitor mode = reference is everything the
  Jabra plays) removes Buddy's **own voice** → `buddy_aec_source`, which ears reads (`BUDDY_EARS_DEVICE`). Its noise
  suppression also damps steady background. Raw vs AEC on the same recordings decides whether it stays ⚠️ (D13; compare
  with `deploy/train.sh eval --source raw|aec`). Config `deploy/config/pipewire/buddy-audio.conf` + `wireplumber/buddy-alsa.conf`
  (bigger USB buffers: AEC puts the 16 kHz mic and 48 kHz speaker on one clock, and with default buffers the speaker
  XRUNs forever). Defaults stay the Jabra.

Done when: with the box at normal volume, "Hey Jarvis" triggers from 1 m and 2–3 m, the box ducks, and the command is right.

| Symptom | Fix |
|---|---|
| Box doesn't duck | `deploy/logs.sh spotify`: device not found (standby?), `refuses remote volume`, or not playing at the wake word. Test: `buddy-spotify --cmd duck` while it plays |
| Box stays quiet | `buddy-spotify --cmd restore`; `systemctl --user status buddy@spotify` |
| Music jumps up before the reply | `BUDDY_SPOTIFY_DUCK_HOLD_MS=2500` |
| Commands misheard over music | `BUDDY_SPOTIFY_DUCK_PERCENT` lower; more box sessions in the training data |
| No "Buddy mic (echo-cancelled)" in `wpctl status` | `setup.sh audio`; `journalctl --user -u pipewire -n 50` |
| ears: `cannot open audio device "pipewire:NODE=…"` | `pipewire-alsa` missing or node name typo |
| No sound at all though `wpctl` shows RUNNING | `grep state /proc/asound/card*/pcm0p/sub0/status` → `XRUN`: `buddy-alsa.conf` missing (`setup.sh audio`) |
| Default sink/source became a Buddy node | `wpctl set-default <Jabra id>` for both |

## 1.12 Boot & hardening (D14)

| Failure | Behaviour |
|---|---|
| Service crashes | Restart after 1 s, backing off to 30 s, never gives up (`StartLimitIntervalSec=0`) |
| Service hangs (ALSA stall, whisper hang, deadlock) | No watchdog ping for 60 s → SIGABRT and restart |
| Mosquitto not up yet at boot | Retry 0.25 → 5 s, log every 30 s; `systemctl status` shows "waiting for the broker" |
| Exception in an MQTT handler | Logged, service keeps running |
| PipeWire restart / Jabra gone | ears and voice reopen their device; ears keeps pinging while it waits |
| Crash core files | Off (`LimitCORE=0`): would be ~250 MB on the microSD |

- `buddy@.service`: `Type=notify`, `WatchdogSec=60`, `Restart=always`, `RestartSteps=5`, `RestartMaxDelaySec=30`,
  `UMask=0077`, `MemoryMax=1G`, `PYTHONUNBUFFERED=1`. `shared/systemd.*` (C++) / `buddy_shared.systemd` (Python) send
  READY/WATCHDOG/STATUS to `$NOTIFY_SOCKET` (no libsystemd); outside systemd they do nothing.
- READY is sent in `Mqtt::start()` (after models and config loaded), so `systemctl start` and `deploy.sh` report the real
  result. Watchdog pings come from each main loop; a new loop without one is killed every 60 s.
- Power cycle passed 2026-10-23: services up at ~8 s, Mosquitto ~14 s, NTP ~35 s (the clock jump makes Mosquitto drop
  every client once; they're back within ~3 s), wake word at ~115 s.
- journald is RAM-only (Pi default): logs are gone after a reboot. `Storage=persistent` + `SystemMaxUse=50M` would keep
  them at a small microSD cost; not done.

| Symptom | Fix |
|---|---|
| `deploy.sh` ends with a service not `active` | `deploy/logs.sh <service>`: bad config or missing model |
| RESTARTS keeps growing | Crash loop: `deploy/status.sh -l`; after fixing, `systemctl --user restart buddy@<name>` resets the counter (it only resets on manual start/stop) |
| `Failed with result 'watchdog'` | Hung ≥ 60 s: look at the lines before |
| Restarts every ~60 s, nothing in the log | Main loop without `notify_watchdog()` |
| Everything reconnects ~35 s after boot | NTP clock jump: harmless |

## 1.13 Hue lights (D18)

```
 ears ──► brain ──buddy/hue/cmd──► hue ──HTTPS (LAN)──► Hue Bridge ──Zigbee──► lights
                                    ▲  └─► voice/say (errors, "are the lights on?")
                                    └── eventstream (Hue app, switches) ──► retained buddy/hue/state ──► web panel
```

- **Local API only:** CLIP v2 on the bridge, no cloud, no Hue account. Requests go to `https://<bridge id>/…`, resolved to
  the bridge's IP; the bridge's certificate is signed by Hue's `root-bridge` CA (compiled into `buddy-hue`) with the
  bridge id as common name, so TLS is fully verified. Checked 2026-10-28 on the BSB002 (firmware 1978545030).
- **Pairing** (once): press the bridge's link button, then within 30 s **Pi:** `~/buddy/bin/buddy-hue --pair`. It finds
  the bridge via `discovery.meethue.com` (Hue limits that to about one call per 15 min) or `BUDDY_HUE_BRIDGE`, and
  writes `~/buddy/config/hue.json` (bridge id, IP, key; mode 600). Then `systemctl --user restart buddy@hue`.
- **State:** on start hue loads every room, zone, light and scene (`GET /clip/v2/resource`); the bridge's eventstream
  keeps it current, also for changes from the Hue app or wall switches. Renames and new lights → full reload.
- **Limits:** PUTs to one light at most every 100 ms, to a room or zone (grouped_light) or scene every 1 s (Hue's
  guidance ⚠️ not measured); what piles up for one target merges, so a slider sends its last value.
- **Who talks:** only hue, and only for errors and status questions. Switching is silent: the light is the answer.

| You say (examples) | Intent → what hue does |
|---|---|
| "turn on the lights", "lights off", "turn everything off" | `on` / `off` → `BUDDY_HUE_DEFAULT_ROOM` (empty: every light); "everything", "all the lights" → every light |
| "living room lights off", "turn on the lights in the kitchen", "switch off desk lamp" | the same for that room, zone or light. Name: exact, contained or a word 1–2 letters off; equal matches: room > zone > light; two → "Which one, …?" |
| "dim the bedroom to 20 %", "set lights to 50 %" | `brightness` (0 = off) |
| "make it brighter", "dim the lights", "turn the lights down" | `brighter` / `darker` (±`BUDDY_HUE_STEP`, 20 %) |
| "turn the lights blue", "set the desk lamp to warm white" | `color`: red, orange, yellow, green, cyan/turquoise, blue, purple/violet, magenta, pink; whites: candle, warm, neutral, cool, daylight, or kelvin |
| "activate the movie scene", "turn on the relax scene in the bedroom" | `scene`: in the named room, else the default room first, then any room |
| "are the kitchen lights on?" | `status` → "The Kitchen lights are on at 40 percent." |

Light rules need a light word ("light(s)", "lamp(s)", "scene", "brighter", "darker", "dim", "everything"), so music
phrasings ("turn it down") stay with spotify.

Offline (**Pi**, load `buddy.env` first as in 1.8/1.9): `buddy-hue --cmd list` (rooms, zones, scenes, lights with exact
names), `buddy-hue --cmd <action> [query] [room]` (real bridge, no MQTT), `buddy-brain --test "Bedroom lights off."`.

| Symptom | Fix |
|---|---|
| "The lights aren't set up yet." / log `not paired yet` | `buddy-hue --pair` (link button first), restart `buddy@hue` |
| `--pair`: `link button not pressed within 30 s` | Press the button, then start `--pair` right away |
| `--pair`: `no bridge found` / discovery HTTP 429 | `BUDDY_HUE_BRIDGE=<ip> buddy-hue --pair` (IP from the router or the Hue app: Settings → Bridges) |
| "I can't reach the Hue bridge." / log `Couldn't connect` | Bridge off or new IP: fixed IP in the router, or `BUDDY_HUE_BRIDGE=<ip>` in `buddy.env` |
| Log `SSL certificate problem` | Bridge with an old self-signed certificate, or a Hue Bridge Pro (other CA): tell Claude (D18) |
| "The Hue bridge doesn't know me anymore." (HTTP 403) | Key deleted in the Hue app: `--pair` again |
| "I don't know a room or light called …" | `--cmd list` for the exact names; whisper mishears them → names into `commands.tsv` and train (`docs/voice-training.md`) |
| A light doesn't react, others do | Light unreachable (no power at the switch): the bridge accepts the command anyway ⚠️ |

## 1.14 Web panel (D19)

`http://buddy.local:8080` (or the Pi's IP) from a phone or PC on the LAN. Rooms with on/off, brightness, warm↔cool,
colours and scenes (each room's single lights behind "N lights ▾"); music play/pause/next/previous and volume; a chip per
service (green = online) with ears/voice activity.

- **web** is an HTTP ⇄ MQTT bridge with no skill logic: it streams every retained `buddy/+/state` and `buddy/+/status` to
  the browser (server-sent events, `/api/events`) and publishes the browser's commands to `buddy/<svc>/cmd`, only for
  command topics in `topics.json`. The page (`services/web/www/`, plain HTML/CSS/JS, installed to `~/buddy/share/web`)
  sends the same commands brain does, plus `id` for hue (no name matching).
- **No login.** Anyone on the Wi-Fi can switch lights and music (like a guest with the Hue app). Protections: only IPs,
  `localhost`, `*.local` names and `BUDDY_WEB_HOSTS` may open it (a web page elsewhere can't reach it through its own
  DNS name), and commands must be JSON from the panel's own origin (another site open in your browser can't send them).
  Don't forward the port on the router.
- Measured 2026-10-28 on the Pi, idle: `buddy-web` 10 MB RSS, `buddy-hue` 14 MB (unpaired), ~0 % CPU. Shutdown with a
  browser connected: ~0.2 s.

| Symptom | Fix |
|---|---|
| Page doesn't load | `deploy/status.sh` (web active?); phone on the same Wi-Fi; `buddy.local` not resolving on Android → use the IP |
| `unknown host name` (403) | Opened via a router DNS name: add it to `BUDDY_WEB_HOSTS` |
| "reconnecting…" at the top | web restarting or the Pi gone; the page reconnects by itself |
| "The hue service isn't running." | `deploy/logs.sh hue` |
| `can't listen on 0.0.0.0:8080` | Port taken: `BUDDY_WEB_PORT` |
