# Buddy – Desk Robot

Stationary desk robot: a voice assistant first ("Hey buddy, play Spotify"), then an expressive head, a robotic hand, and finally an arm with vision.

This file is the single source of truth for roadmap, architecture, decisions, progress and working rules. It replaces `buddy-build-guide.md`.

## Files

| File | Purpose |
|---|---|
| `CLAUDE.md` | Roadmap, architecture, decisions, progress, rules (this file) |
| `buddy-hardware.md` | Current parts and tools |
| `guides/XX-YY-name.md` | Detailed guide per step, e.g. `guides/01-02-assembly.md` |

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
| 1 – Voice | 1.1 Order hardware | ✅ |
| | 1.2 Assemble the Pi | ⬜ |
| | 1.3 OS setup | ⬜ |
| | 1.4 Audio setup | ⬜ |
| | 1.5 Project skeleton, MQTT & dev workflow | ⬜ |
| | 1.6 Wake word | ⬜ |
| | 1.7 Speech-to-text | ⬜ |
| | 1.8 Brain / intent router | ⬜ |
| | 1.9 Spotify skill | ⬜ |
| | 1.10 Voice replies (TTS) | ⬜ |
| | 1.11 Music ducking & echo cancellation | ⬜ |
| | 1.12 Run on boot & hardening | ⬜ |
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

Update this table when a decision is made or changed.

---

## Architecture (target)

```
 Jabra mic ─► [ears]  wake word → VAD → speech-to-text ──┐
                                                         ▼
                                          MQTT  ◄──► [brain]  intent → skill routing
                                                         │
        ┌────────────────┬───────────────────────────────┼────────────────┐
        ▼                ▼                               ▼                ▼
   [voice] TTS     [spotify] skill                [motion] ──USB──► Pico (servos, eyes)
        │                │
        └──► Jabra speaker ◄──┘ (librespot)
```

- Every component is a small, independent service with one job.
- Services communicate only over MQTT (Mosquitto on the Pi).
- All services are C++ (D5). MQTT topic names are defined once in `shared/` and included by every service.
- ears and brain must be host-agnostic: broker address comes from config/env, never hard-coded.
- Microphone and speaker are the same USB device (Jabra Speak 510 MS).

---

## Phase 1 – Voice assistant

| Step | Goal | Key tools | Done when |
|---|---|---|---|
| 1.1 Order hardware | All parts on the desk | Spotify Premium account (Web API) | ✅ |
| 1.2 Assemble the Pi | Cooler fitted, Jabra connected, safe placement | Active Cooler | The Pi powers on |
| 1.3 OS setup | Headless OS booting from microSD | Raspberry Pi Imager, RPi OS Lite 64-bit, SSH. Hostname `buddy`, SSH key and Wi-Fi set in Imager. | `ssh buddy.local` works |
| 1.4 Audio setup | Record from and play through the Jabra | PipeWire / WirePlumber, `pw-record` / `pw-play`, `arecord` / `aplay` | A test recording plays back clearly |
| 1.5 Skeleton, MQTT & workflow | Repo, message bus, service template, edit→build→deploy→test loop | Git (Gitea), Mosquitto, CMake, CLion Full Remote Mode, runtime per D7 | A dummy service publishes, another receives |
| 1.6 Wake word | Buddy reacts to "Hey buddy" | openWakeWord models run via ONNX Runtime C++ (rhasspy's archived `openwakeword-cpp` as reference). Stock model ("hey jarvis") first. A custom "Hey buddy" model is optional: trained once in a hosted notebook (Google Colab), which outputs an `.onnx` file. No Python in the repo or on the Pi. | Triggers reliably from 2–3 m, few false positives |
| 1.7 Speech-to-text | Command after wake word → text | Silero VAD (end of speech, via ONNX Runtime C++) + whisper.cpp. English or High German. Pick model size by speed vs accuracy. | "Play Spotify" arrives on MQTT within ~1–2 s |
| 1.8 Brain | Text → intent → skill topic | Keyword/regex rules (play, pause, next, louder, quieter) behind an interface an LLM can replace later | Transcripts routed to the right skill topic |
| 1.9 Spotify skill | Control Spotify by voice | raspotify / librespot (Pi = Spotify Connect device), Spotify Web API (Development Mode) via a C++ HTTP client, OAuth refresh, audio via Jabra | "Hey buddy, play Spotify / pause / next song" works |
| 1.10 Voice replies | Buddy talks back | Piper via libpiper (C++). Short confirmations. | Buddy answers commands audibly |
| 1.11 Ducking & AEC | Buddy hears you over music | Volume ducking/pause on wake word. PipeWire echo-cancel (WebRTC AEC) only if needed — test the Jabra's own behaviour first. | Wake word works over music at normal volume |
| 1.12 Boot & hardening | Starts alone, recovers from errors | systemd units, journald, restart policies, audio device detection after boot | After a power cycle, "Hey buddy, play Spotify" works untouched |

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

## Repo structure (planned)

```
buddy/
├── CLAUDE.md                ← this file
├── buddy-hardware.md        ← current hardware & software
├── guides/                  ← one guide per step
├── shared/                  ← MQTT topic constants (one header) + shared C++ helpers
├── services/
│   ├── ears/                ← wake word, VAD, STT
│   ├── brain/               ← intent routing
│   ├── voice/               ← TTS
│   ├── spotify/             ← Spotify skill
│   └── motion/              ← Pi side of motor control
├── firmware/
│   └── pico/                ← servo & display control
├── cad/                     ← Onshape exports (STEP)
└── deploy/                  ← systemd units, configs, deploy scripts
```

---

## Dev workflow

| Loop | How |
|---|---|
| Inner (try things) | CLion Full Remote Mode → Run/Debug. Syncs to `~/buddy/dev` on the Pi, builds Debug there, runs/debugs there. Stop the deployed unit first (same MQTT client id). |
| Keep | `git commit && git push` (Gitea) |
| Deploy | `deploy/deploy.sh [service…]` → rsync to `~/buddy/src`, Release build, install to `~/buddy/bin`, restart `buddy@<service>` |
| Test | `deploy/logs.sh <service>`, `ssh buddy "mosquitto_sub -v -t 'buddy/#'"` |
| First-time Pi setup | `deploy/deploy.sh --setup` (runs `deploy/setup-pi.sh`) |

Pi paths: `~/buddy/{dev,src,build-release,bin,config}`. Runtime config: `~/buddy/config/buddy.env` (not in Git). SSH alias `buddy` → `dev@buddy.local`.

## Code conventions

- C++20, CMake + Ninja, `-Wall -Wextra -Wpedantic`. Dependencies from apt where possible.
- One executable per service: `services/<name>/` → target `buddy-<name>` → unit `buddy@<name>`.
- Topics: `buddy/<service>/<name>`, JSON payloads, all names in `shared/include/buddy/topics.hpp`. Events are published only by the owning service, commands consumed only by it.
- Every service: `buddy/<service>/status` = `online`/`offline` (retained, last will). MQTT client id `buddy-<service>`.
- Config only via `BUDDY_*` environment variables. Never hard-code hosts.
- Log to stdout (journald). Keep logs quiet in steady state: microSD wear.
- New service checklist: folder + CMake target with `install()`, `add_subdirectory`, topics in `topics.hpp`, name in `SERVICES` in `deploy.sh`.

## Golden rules

1. **Never power servos from the Pi.** Always a separate supply with a shared ground.
2. **One service, one job.** Communicate only over MQTT.
3. **Get it working, then make it nice.** Simple rules before LLMs, cardboard before 3D prints.
4. **Update the progress table** whenever a step is done.

---

## How to work

1. Anchor every answer to the current step in the progress table. Don't jump ahead unless asked, but flag decisions that affect later phases (e.g. MQTT topic names that motion will reuse).
2. Search before giving install commands, versions or API details. These change fast: Raspberry Pi OS, PipeWire, openWakeWord, ONNX Runtime, whisper.cpp, Piper / libpiper, CLion, librespot / raspotify, Spotify Web API (Development Mode limits).
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
