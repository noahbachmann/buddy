# Buddy – Desk Robot

Stationary desk robot: a voice assistant first ("Hey buddy, play Spotify"), then an expressive head, a robotic hand, and finally an arm with vision.

This file is the single source of truth for roadmap, architecture, decisions, progress and working rules. It replaces `buddy-build-guide.md`.

## Files

| File | Purpose |
|---|---|
| `CLAUDE.md` | Roadmap, architecture, decisions, progress, rules (this file) |
| `buddy-hardware.md` | Current parts and tools |
| `guides/XX-YY-name.md` | Detailed guide per step, e.g. `guides/01-12-boot-hardening.md`. Older finished steps are condensed into `guides/01-02-06-summary.md`; keep the 5 newest guides in full |
| `skeleton/CLAUDE.md` | Code quick reference: service pattern, shared helpers, test entry points |

- Read this file and `buddy-hardware.md` before answering about setup, parts or next steps.
- The progress table is the current status.
- If a request contradicts a file, point it out and ask which is correct.

---

## About Dev

- Strong: C++, Kotlin, Python, Linux, Docker, Git. **Prefers C-like languages over Python.**
- New to: 3D printing, mechanical CAD, soldering (never done it), servos, power electronics, Linux audio.
- Skip basics for software, Git, Docker and shell. Explain hardware, electronics and printing step by step, including *why*.
- Based in Switzerland: prefer CH suppliers (Digitec Galaxus, Bastelgarage, pi-shop.ch) and CHF prices.
- Docs in English. Chat in the language Dev writes in.

---

## Progress

| Phase | Step | Status |
|---|---|---|
| 1 – Voice | 1.1 – 1.10 Hardware, OS, audio, skeleton, wake word, STT, brain, Spotify, voice | ✅ |
| | 1.11 Music ducking & echo cancellation (Dev, needs voice recordings) | 🟨 |
| | 1.12 Run on boot & hardening (open: Dev's power-cycle test) | 🟨 |
| 2 – Head | 2.1 – 2.6 | ⬜ |
| 3 – Hand | 3.1 – 3.4 | ⬜ |
| 4 – Arm & vision | 4.1 – 4.3 | ⬜ |

Legend: ⬜ not started · 🟨 in progress · ✅ done

---

## Decisions

| ID | Topic | Decision | Status |
|---|---|---|---|
| D1 | OS | Raspberry Pi OS Lite 64-bit (Debian 13 "Trixie"), headless | ✅ |
| D2 | Message bus | Mosquitto on the Pi. Services talk only over MQTT. | ✅ |
| D3 | Compute placement | ears and brain run on the Pi *or* a LAN host. Only the broker address changes. | ✅ |
| D4 | Storage | microSD only, no NVMe | ✅ |
| D5 | Languages | C++ only (CMake), for all services and the Pico firmware. No Python, Kotlin or Java in the repo. | ✅ |
| D6 | Dev workflow | CLion on the PC, Full Remote Mode over SSH: sources stay local (and in Gitea), CLion syncs them to the Pi, builds with the Pi's CMake/GCC, runs and debugs with the Pi's GDB. | ✅ |
| D7 | Runtime packaging | systemd user units on the Pi (`buddy@<service>`, lingering on), Mosquitto from apt. Docker only for services offloaded to a LAN host later. | ✅ |
| D8 | Audio capture | ears reads the echo-cancelled PipeWire source via ALSA (`pipewire:NODE=buddy_aec_source`, D13), 16 kHz mono s16. Wake word, VAD and STT live in one service (`ears`) sharing that stream. | ✅ |
| D9 | ML libraries | ONNX Runtime: Microsoft's prebuilt aarch64 release in `/opt/onnxruntime` (Debian's package spams the log). whisper.cpp: pinned tag built via CMake FetchContent. Models in `~/buddy/models` via `deploy/fetch-models.sh`, not in Git. | ✅ |
| D10 | Spotify | Spotify Soloist (official headless Connect client) as user unit `buddy-soloist`, binary fetched from Spotify by `deploy/soloist-update.sh` + weekly timer (builds expire after 90 days). Closed source → systemd-sandboxed (sees only its own folders), key in `~/buddy/config/soloist.env`. Not librespot: Spotify refuses it audio keys on many accounts. Web API via libcurl, OAuth PKCE, refresh token in `~/buddy/config/spotify-token.json`. | ✅ |
| D11 | Intents & replies | brain: regex rules behind `IntentParser` (LLM-replaceable), routes to `buddy/<skill>/cmd`. The service that knows the outcome publishes `buddy/voice/say`. | ✅ |
| D12 | TTS | libpiper v1.8.0 built from source by `setup-pi.sh` into `/opt/libpiper` (against `/opt/onnxruntime`); voices via `fetch-models.sh`. | ✅ |
| D13 | Echo cancellation & ducking | PipeWire echo-cancel (WebRTC, monitor mode = reference is everything the Jabra plays) → `buddy_aec_source`. Music plays into virtual sink `buddy_music`; service `mixer` (Pi only) turns it down via `pactl` while ears listens or voice speaks. Config `deploy/pipewire/buddy-audio.conf`. Defaults stay the Jabra. | ✅ |
| D14 | Recovery | `buddy@.service` is `Type=notify` with `WatchdogSec=60` (READY from `Mqtt::start`, pings from each main loop, `shared/systemd.hpp`, no libsystemd). Crashes restart with backoff 1 → 30 s and never give up. No core files. Soloist starts after NTP sync. | ✅ |

Update this table when a decision is made or changed.

---

## Architecture (target)

```
 Jabra mic ─► echo-cancel ─► [ears]  wake word → VAD → speech-to-text ──┐
                                                                        ▼
                                                         MQTT  ◄──► [brain]  intent → skill routing
                                                                        │
        ┌────────────────┬──────────────────────────────────────────────┼────────────────┐
        ▼                ▼                                              ▼                ▼
   [voice] TTS     [spotify] skill                               [motion] ──USB──► Pico (servos, eyes)
        │                │
        │          Soloist ─► buddy_music sink ◄── [mixer] ducks it while ears listens / voice speaks
        │                         │
        └──► Jabra speaker ◄──────┘
```

- Every component is a small, independent service with one job.
- Services communicate only over MQTT (Mosquitto on the Pi).
- All services are C++ (D5). MQTT topic names are defined once in `shared/` and included by every service.
- ears and brain must be host-agnostic: broker address comes from config/env, never hard-coded.
- Microphone and speaker are the same USB device (Jabra Speak 510 MS).

---

## Phase 1 – Voice assistant

| Step | Result | Guide |
|---|---|---|
| 1.1 – 1.6 | Pi assembled, OS, PipeWire audio, C++ skeleton + MQTT + CLion workflow, openWakeWord ("hey jarvis") in ears | `guides/01-02-06-summary.md` |
| 1.7 Speech-to-text | Silero VAD + whisper.cpp (`ggml-base.en`) in ears → `buddy/ears/transcript` | `guides/01-07-speech-to-text.md` |
| 1.8 / 1.9 Brain & Spotify | Regex intents → `buddy/spotify/cmd`; Soloist + Web API (OAuth PKCE) | `guides/01-08-brain-and-spotify.md` |
| 1.10 Voice | libpiper TTS, `buddy/voice/say` | `guides/01-10-voice.md` |

| Open step | Goal | Done when | Guide |
|---|---|---|---|
| 1.11 Ducking & AEC (Dev) | Buddy hears you over music: PipeWire echo-cancel, ducking via mixer; optional STT fine-tuning on own voice | Wake word works over music at normal volume | `guides/01-11-ducking-echo-cancel.md`, `guides/01-11-custom-stt.md` |
| 1.12 Boot & hardening | Starts alone, recovers from crashes/hangs/audio hiccups (D14) | After a power cycle, "Hey Jarvis, play Spotify" works untouched | `guides/01-12-boot-hardening.md` |

### Deferred: needs voice recordings (pick up with/after 1.11)

Measure on real clips (`deploy/record-voice.sh`, then `deploy/stt-eval.sh` / `buddy-ears --wav`), never on synthetic speech alone.

- **STT speed:** whisper's encoder always processes a 30 s window. `whisper_full_params.audio_ctx` sized to the command
  (≈ 50 per second of audio + margin) should cut `stt_ms` a lot; risk: accuracy on very short commands. Also
  `temperature_inc = 0` (no re-decoding on uncertain results: lower worst-case latency) and model size (tiny/base/small).
- **Wake word CPU:** ears idles at ~8 % of one core. Profiled 2026-10-23: `embedding_model.onnx` is 3.7 of 4.2 ms per 80 ms
  chunk, glue code ~0.25 ms, denormal flushing no effect. Only behaviour-changing options remain: quantized embedding
  model, or skipping inference during silence. Both need score comparisons on recordings.
- **Tuning:** `BUDDY_WAKE_THRESHOLD`/`PATIENCE`, VAD silence and level gate, custom "Hey buddy" (summary guide).
- **1.11 on the Pi:** `pactl` is missing (`pulseaudio-utils` not installed): mixer can't duck until `deploy.sh --setup` runs.
  `BUDDY_EARS_DEVICE` still `default` (raw mic, no echo cancellation).

## Phase 2 – Head & personality

| Step | Summary | Guide |
|---|---|---|
| 2.1 3D printer | Set up the Bambu Lab P2S. Print tolerance tests (holes, clearances) to find CAD offsets. | `guides/02-01-printer.md` |
| 2.2 Motion controller | Pico + PCA9685 servo driver + separate 5–6 V servo supply. USB serial link to the Pi. | `guides/02-02-motion-controller.md` |
| 2.3 Motion service | `[motion]`: MQTT commands → Pico serial protocol. | `guides/02-03-motion-service.md` |
| 2.4 Eyes | Round display (e.g. GC9A01) with states: idle, listening, thinking, playing. | `guides/02-04-eyes.md` |
| 2.5 Head pan/tilt | Two servos. Head turns toward the user on wake word. | `guides/02-05-head.md` |
| 2.6 Head & body design | Onshape CAD, print head and body housing the Pi (PLA prototypes, PETG HF final). | `guides/02-06-body-design.md` |

## Phase 3 – Hand

| Step | Summary | Guide |
|---|---|---|
| 3.1 Hand design | Start from the InMoov hand (tendon-driven), remix as needed. | `guides/03-01-hand-design.md` |
| 3.2 Print & assemble | Fingers and palm, tendons, 5 servos. | `guides/03-02-hand-build.md` |
| 3.3 Hand control | Finger positions and gestures in the motion service. | `guides/03-03-hand-control.md` |
| 3.4 Voice gestures | "Hey buddy, wave / high five". | `guides/03-04-gestures.md` |

## Phase 4 – Arm & vision

| Step | Summary | Guide |
|---|---|---|
| 4.1 Arm | 2–3 joints, stronger servos, mounted to the body. | `guides/04-01-arm.md` |
| 4.2 Camera | Pi Camera Module 3, face detection. | `guides/04-02-camera.md` |
| 4.3 Face tracking | Head follows the user. | `guides/04-03-face-tracking.md` |

---

## Repo structure

```
buddy/
├── CLAUDE.md                ← this file
├── buddy-hardware.md        ← current hardware & software
├── guides/                  ← one guide per step
├── skeleton/                ← C++ project root (CMake); paths below and in "Dev workflow" are relative to it
│   ├── shared/              ← MQTT topic constants (one header) + shared C++ helpers
│   ├── third_party/         ← ONNX Runtime, libpiper (prebuilt in /opt), whisper.cpp (FetchContent)
│   ├── services/
│   │   ├── ears/            ← wake word, VAD, STT
│   │   ├── brain/           ← intent routing
│   │   ├── voice/           ← TTS
│   │   ├── spotify/         ← Spotify skill
│   │   ├── mixer/           ← music ducking
│   │   └── motion/          ← Pi side of motor control (planned)
│   └── deploy/              ← systemd units, PipeWire config, setup/deploy/model scripts
├── parts/                   ← printable parts: STLs, arm/ in parametric OpenSCAD
└── firmware/pico/           ← servo & display control (planned)
```

---

## Dev workflow

| Loop | How |
|---|---|
| Inner (try things) | CLion Full Remote Mode → Run/Debug. Syncs to `~/buddy/dev` on the Pi, builds Debug there, runs/debugs there. Stop the deployed unit first (same MQTT client id). |
| Keep | `git commit && git push` (Gitea) |
| Deploy | `deploy/deploy.sh [service…]` → rsync to `~/buddy/src`, Release build, install to `~/buddy/bin`, restart `buddy@<service>` |
| Test | `deploy/logs.sh <service>`, `deploy/status.sh [-l]`, `ssh buddy "mosquitto_sub -v -t 'buddy/#'"` |
| First-time Pi setup | `deploy/deploy.sh --setup` (runs `deploy/setup-pi.sh`) |

Pi paths: `~/buddy/{dev,src,build-release,bin,config}`. Runtime config: `~/buddy/config/buddy.env` (not in Git). SSH alias `buddy` → `dev@buddy.local`.

## Code conventions

- C++20, CMake + Ninja, `-Wall -Wextra -Wpedantic`. Dependencies from apt where possible.
- One executable per service: `services/<name>/` → target `buddy-<name>` → unit `buddy@<name>`.
- Topics: `buddy/<service>/<name>`, JSON payloads, all names in `shared/include/buddy/topics.hpp`. Events are published only by the owning service, commands consumed only by it.
- Every service: `buddy/<service>/status` = `online`/`offline` (retained, last will). MQTT client id `buddy-<service>`.
- Config only via `BUDDY_*` environment variables. Never hard-code hosts.
- Log to stdout (journald). Keep logs quiet in steady state: microSD wear.
- New service checklist: folder + CMake target with `install(… COMPONENT <name>)`, `add_subdirectory`, topics in `topics.hpp`, name in `SERVICES` in `deploy.sh`, `systemd::notify_watchdog()` in the main loop (D14).

## Golden rules

1. **Never power servos from the Pi.** Always a separate supply with a shared ground.
2. **One service, one job.** Communicate only over MQTT.
3. **Get it working, then make it nice.** Simple rules before LLMs, cardboard before 3D prints.
4. **Update the progress table** whenever a step is done.

---

## How to work

1. Anchor every answer to the current step in the progress table. Don't jump ahead unless asked, but flag decisions that affect later phases (e.g. MQTT topic names that motion will reuse).
2. Search before giving install commands, versions or API details. These change fast: Raspberry Pi OS, PipeWire, openWakeWord, ONNX Runtime, whisper.cpp, Piper / libpiper, CLion, Spotify Soloist, Spotify Web API (Development Mode limits).
3. Enforce the golden rules. Warn clearly about anything risky: power, wiring, heat, servo current.
4. Small runnable increments. Every step ends with a concrete test matching its "Done when".
5. State assumptions. Ask at most one question when blocked.

### Pi 5 specifics

- aarch64: check that every library/binary has an arm64 build.
- Audio goes through PipeWire.
- The 27 W supply is required for the full 1.6 A USB current budget.
- The Active Cooler is fitted permanently; don't remove it.
- Never pull power while the OS runs (`sudo poweroff` first): risk of microSD corruption.

---

## Output formats

- **Step guides** (`guides/XX-YY-name.md`): Goal → Prerequisites → Parts/Tools → Steps (copy-pasteable commands with expected output) → Verify → Troubleshooting → Notes for later phases. Mark untested assumptions with ⚠️.
- **Code:** C++ with CMake. MQTT topic constants live in one header in `shared/`. Broker address and other host-specific values come from config/env.
- **CAD/printing** (Onshape, Bambu P2S): dimensions with clearance offsets, print orientation, supports. PLA for prototypes, PETG HF near the Pi or motors.
- **Style:** concise, no filler. Tables for BOMs and pinouts, code blocks for commands.

## Maintenance

- When a step is done: output the updated progress-table row(s) for this file.
- When hardware changes: output the updated lines for `buddy-hardware.md`.
- When a decision changes: output the updated row of the Decisions table.
