# Buddy – Desk Robot

Stationary desk robot: a voice assistant first ("Hey buddy, play Spotify"), then an expressive head, a robotic hand, and finally an arm with vision.

This file is the single source of truth for roadmap, architecture, decisions, progress and working rules.

## Files

| File | Purpose |
|---|---|
| `CLAUDE.md` | Roadmap, architecture, decisions, progress, open work, rules (this file) |
| `README.md` | Front page for Dev: what Buddy is, where to read what, everyday commands (`deploy/run.sh` examples) |
| `buddy-hardware.md` | Current parts and tools |
| `docs/guides/XX-YY-name.md` | Detailed guide per step of the current phase, e.g. `docs/guides/02-01-printer.md` |
| `docs/plans/XX-YY-name.md` | Plan for a step before its guide exists: decisions, parts, order of work; replaced by the guide |
| `docs/plans/05-riftbound.md` | Phase 5 plan: Riftbound against Dev (engine, AI, hand pile model, `game`/`table` services, steps) |
| `docs/plans/02-06-cad-prompt.md` | Prompt Dev fills in to get the neck, head and body CAD (2.5 stage 1, 2.6 stage 2) |
| `docs/guides/0N-setup-summary.md` | A finished phase condensed into one file (rebuild, config, tests, troubleshooting); replaces its step guides |
| `docs/voice-training.md` | Recurring workflow: record, train, score and install speech-to-text on Dev's voice |
| `skeleton/CLAUDE.md` | Code quick reference: service pattern (C++ and Python), shared helpers, deploy scripts, tests |
| `skeleton/services/web/CLAUDE.md` | Web panel internals: server, state stream, `www/` page structure |

- Read this file and `buddy-hardware.md` before answering about setup, parts or next steps.
- The progress table is the current status.
- Work only Dev can do at the end of a phase (recordings, hardware tests) goes into one temporary guide
  (`docs/guides/0N-cleanup.md`) that Dev deletes when done; every other doc describes the state after it.
- If a request contradicts a file, point it out and ask which is correct.

---

## About Dev

- Strong: C++, Kotlin, Python, Linux, Docker, Git. **Prefers C-like languages over Python.**
- New to: 3D printing, mechanical CAD, soldering (never done it), servos, power electronics, Linux audio.
- Skip basics for software, Git, Docker and shell. Explain hardware, electronics and printing step by step, including *why*.
- Wants harsh reviews and the optimal, clean solution, not the easy one; say clearly when Dev needs to do more (e.g. record more data).
- Based in Switzerland: prefer CH suppliers (Digitec Galaxus, Bastelgarage, pi-shop.ch) and CHF prices.
- Docs in English. Chat in the language Dev writes in.

---

## Progress

| Phase | Step | Status |
|---|---|---|
| 1 – Voice | 1.1 – 1.12 Hardware, OS, audio, skeleton, wake word, STT, brain, Spotify, voice, background music, boot | ✅ |
| 1 – Voice (extra) | 1.13 Hue lights, 1.14 web panel (#6), 1.15 Nest speakers over Bluetooth (D20, #17): done 2026-10-30 (side issue: Deferred → Bathroom Bluetooth) | ✅ |
| 2 – Head | 2.1 printer: guide not written yet. 2.2 motion controller: Pi setup, firmware and flash done; wiring and servo test (guide steps 1–3, 9) open. 2.3 motion service + demo gesture ("cut off Andi's balls"): code and tests done, deployed 2026-10-29 (waits with `connected:false` while the Pico is unplugged); Verify open. 2.4 eyes: postponed (Dev, 2026-10-30). 2.5 head pan/tilt and 2.6 head & body: plans in `docs/plans/` | 🟨 |
| 3 – Hand | 3.1 – 3.4 | ⬜ |
| 4 – Arm & vision | 4.1 – 4.3 | ⬜ |
| 5 – Riftbound | plan in `docs/plans/05-riftbound.md` (#18); 5.1 – 5.6 software against a simulated table now, 5.7+ hardware after Phase 4 | ⬜ |

Legend: ⬜ not started · 🟨 in progress · ✅ done

## Open work

What's left right now, in order. Update it with the progress table; GitHub issues hold the details of code work.

**Dev** (needs the hardware, the voice or the room):

1. **Motion live** (2.2 → 2.3): wire supply, Pico and a servo on ch0 (2.2 steps 1–3), plug the Pico into the Pi,
   picocom test (2.2 step 9), then 2.3 Verify (incl. the demo gesture). Then tick 2.2 and 2.3 in the progress table.
2. **Live check** for #12: "Hey Jarvis, play Spotify" ducks, plays, and Buddy answers without delay (idle loops changed).
3. **Decisions** in `docs/plans/02-05-head-pan-tilt.md` and `02-06-head-body.md` (end of each), then fill in
   `docs/plans/02-06-cad-prompt.md` (measurements after 2.1 and the parts order).

**Claude** (code and guides):

1. #13 voice seek ("fast-forward 30 seconds", "go back 2 minutes").
2. 2.1 printer guide (tolerance tests for CAD offsets): the 2.5 neck module is the first print that needs them.
3. 2.5 / 2.6 per `docs/plans/` once Dev has made their decisions: neck module CAD (prompt stage 1), joint table,
   `behaviour` service; then body CAD (stage 2). 2.4 eyes later (decide: Pico or Pi drives the display, 2.2 → Notes
   for later phases).
4. Phase 5 (#18) per `docs/plans/05-riftbound.md`, runs in parallel with the hardware steps: 5.1 card data and the two
   decklists first (Dev checks the lists against the boxes).

Phase 1 → Deferred lists what waits for a reason (wake word CPU, next voice session, custom "Hey buddy", LLM brain).

---

## Decisions

| ID | Topic | Decision | Status |
|---|---|---|---|
| D1 | OS | Raspberry Pi OS Lite 64-bit (Debian 13 "Trixie"), headless | ✅ |
| D2 | Message bus | Mosquitto on the Pi. Services talk only over MQTT. | ✅ |
| D3 | Compute placement | ears and brain run on the Pi *or* a LAN host. Only the broker address changes. | ✅ |
| D4 | Storage | microSD only, no NVMe | ✅ |
| D5 | Languages | C++ (CMake) by default, for services and the Pico firmware. Python where its ecosystem matters: PC tooling and training (`skeleton/tools/`), and Pi services that need Python libraries (LLM, vision later), with the same conventions as C++ services (D15). No Kotlin or Java. | ✅ |
| D6 | Dev workflow | CLion on the PC, Full Remote Mode over SSH: sources stay local (and on GitHub), CLion syncs them to the Pi, builds with the Pi's CMake/GCC, runs and debugs with the Pi's GDB. | ✅ |
| D7 | Runtime packaging | systemd user units on the Pi (`buddy@<service>`, lingering on), Mosquitto from apt. Docker only for services offloaded to a LAN host later. | ✅ |
| D8 | Audio capture | ears reads the raw Jabra mic via ALSA `default` (PipeWire's default source, no echo cancellation: D13), 16 kHz mono s16. Wake word, VAD and STT live in one service (`ears`) sharing that stream. | ✅ |
| D9 | ML libraries | ONNX Runtime: Microsoft's prebuilt aarch64 release in `/opt/onnxruntime` (Debian's package spams the log). whisper.cpp: pinned tag built via CMake FetchContent. Models in `~/buddy/models` via `deploy/pi/fetch-models.sh` (what `buddy.env` names), not in Git; own models (fine-tuned whisper `ggml-buddy-*`) come from the PC. | ✅ |
| D10 | Spotify | Music never through the Jabra. Default speaker `BUDDY_SPOTIFY_DEVICE` = the Main speaker Nest (D20). Speakers by name ("… on the bathroom speaker"; brain splits off the name, spotify matches it against the device list): the Nests plus whatever Spotify sees (DESKTOP-1, Pixel 8); pause/next/volume follow the device that is playing. The web panel shows and controls whatever plays on any device and moves it between speakers (by device id; polled every 3 s only while a panel is open). spotify controls them via the Web API (libcurl, OAuth PKCE, refresh token in `~/buddy/config/spotify-token.json`). Spotify Soloist on the Pi plays for the Nests (back 2026-10-30, D20). | ✅ |
| D11 | Intents & replies | brain: regex rules behind `IntentParser` (LLM-replaceable), routes to `buddy/<skill>/cmd`. `skeleton/tools/voice/commands.tsv` is the phrasing it must understand (ctest). The service that knows the outcome publishes `buddy/voice/say`. | ✅ |
| D12 | TTS | libpiper v1.8.0 built from source by `setup.sh` into `/opt/libpiper` (against `/opt/onnxruntime`), patched to read its ONNX thread count from `PIPER_INTRA_OP_THREADS` (stock: 1; voice sets 3 via `BUDDY_VOICE_THREADS`, first audio ~830 → ~450 ms); voice `en_GB-alan-medium`. | ✅ |
| D13 | Background music, no echo cancellation | The box's music is room noise to Buddy. While ears listens or voice speaks the music is ducked: on a Nest by speakers on the Pi (`buddy_music`, D20), on any other device in the room by spotify via the Web API. **No echo cancellation** (removed 2026-10-28): on the same test session the model trained on the raw mic beat the echo-cancelled one (intent 50 vs 47 of 59, WER 7.4 vs 9.5 %), and its only job, removing Buddy's own voice, is covered by ears ignoring the wake word while voice speaks. Stock PipeWire, training on the raw track. | ✅ |
| D14 | Recovery | `buddy@.service` is `Type=notify` with `WatchdogSec=60` (READY from `Mqtt::start`, pings from each main loop, `shared/systemd.*` / `buddy_shared.systemd`, no libsystemd). Crashes restart with backoff 1 → 30 s and never give up. No core files. | ✅ |
| D15 | Python services | uv workspace in `skeleton/` (`pyproject.toml` + `uv.lock`, members listed explicitly). `shared/python/buddy_shared` mirrors the C++ shared helpers; `shared/topics.json` is the single topic list (CMake generates `topics.hpp`). On the Pi: uv in `~/.local/bin` (pinned), Debian's Python, one venv `~/buddy/venv`, console script linked into `~/buddy/bin`, same unit template. | ✅ |
| D16 | Voice data | Recordings are personal data and the repo is public: never in Git. Master copy on Dev's PC in `~/buddy-voice` (backed up); the Pi keeps sessions only until pulled. Training runs on Dev's PC (NVIDIA GPU, CUDA 12.8 PyTorch). | ✅ |
| D17 | Motion controller | Raspberry Pi Pico 2 H on the Pi's USB, servos on its GPIO hardware PWM (50 Hz, 16 outputs), no PCA9685. Firmware C++ with Pico SDK 2.3.1 (FetchContent) in `skeleton/firmware/pico-motion` (one folder per Pico role: `pico-<job>`; this one drives every servo: head, hand, arm), built and flashed on the Pi (`deploy.sh --firmware`, picotool 2.3.1 in `/opt/picotool`). Text protocol v1 over USB CDC (`/dev/buddy-pico`), header shared with motion. The Pico enforces per-channel pulse limits and relaxes all servos 2 s after the last command. Servo power: separate 5 V supply, shared ground, never via the Pi or the Pico's VBUS/VSYS. | ✅ |

| D18 | Lights | Philips Hue via the Hue Bridge's local CLIP v2 API (no cloud, no Hue account): service `hue`. TLS verified against Hue's `root-bridge` CA (compiled in) with the bridge id as host name (`CURLOPT_RESOLVE` → bridge IP). Application key from `buddy-hue --pair` (link button) in `~/buddy/config/hue.json`. Live state from the bridge's eventstream → retained `buddy/hue/state`. Hue Bridge Pro would need its newer CA ⚠️. | ✅ |
| D20 | Nest speakers | The two Nests play over **Bluetooth A2DP** (SBC) from the Pi: Spotify Soloist ("Buddy", hidden; sandboxed, weekly build update) → PipeWire sink `buddy_music` → the selected Nest; Buddy's voice → `buddy_voice` → that Nest while music plays there (60 %), else the Jabra. Service `speakers` (C++, `pactl`/`pw-link`/`bluetoothctl`, event-driven): connects on demand, one Nest at a time (the Pi refuses a second A2DP link), disconnects after 10 min without music, ducks `buddy_music` locally. spotify lists the Nests (`bt:<mac>`) in Soloist's place. Not Cast: Buddy can't start Spotify on an idle Nest (403 without Spotify's own desktop login), Soloist can't cast, Cast streaming lags ≥ 3.9 s (#16). | ✅ |
| D19 | Web panel | Service `web` (C++, cpp-httplib pinned via FetchContent): static page + HTTP⇄MQTT bridge, no skill logic. Retained `buddy/+/state` and `/status` → browser (server-sent events); `POST /api/cmd/<svc>` → `buddy/<svc>/cmd` (non-planned command topics only). `http://buddy.local:8080`, LAN only, **no login**: Host allowlist (IP, `*.local`, `BUDDY_WEB_HOSTS`) against DNS rebinding, JSON-only same-origin POSTs against other sites in the same browser. Add auth before anything that moves (motion) goes on the page: `"web": false` in topics.json keeps `buddy/motion/cmd` off it. | ✅ |
| D21 | Card game | Riftbound (Phase 5): **own C++ rules engine, no LLM**: code is the referee, search (heuristic, then ISMCTS over Dev's hidden cards) picks the plays. alpharune is reading material only (no license). Cards scoped to two precons (Vi, Unleashed; Zed, Vendetta); an unimplemented card is a hard error. Rules: Core Rules 1.4. Card data from the Riftcodex API, fetched on the Pi. Services `game` (rules, AI, which cards are in hand) and `table` (physical piles, hand order, scan/recover; sim backend now, hardware later). | ✅ |

Update this table when a decision is made or changed.

---

## Architecture (target)

```
 Jabra mic ─────────────────► [ears]  wake word → VAD → speech-to-text ──┐
                                                                        ▼
                                                         MQTT  ◄──► [brain]  intent → skill routing
                                                                        │
        ┌────────────────┬──────────────────────────┬───────────────────┼────────────────┐
        ▼                ▼                          ▼                   ▼                ▼
   [voice] TTS     [spotify] skill ──Web API──►  [hue] skill ──LAN──►  [motion] ──USB──► Pico (servos, eyes)
        │           Spotify devices; "Buddy" =     Hue Bridge
        │           Soloist on the Pi              (eventstream back)
        ▼                │ music
   buddy_voice      buddy_music ──► [speakers] ──Bluetooth──► Nest (on demand, music ducked while Buddy listens/speaks)
        └──────────────────────────► [speakers] ──► that Nest while music plays there, else the Jabra speaker
                                        phone / PC browser ◄──HTTP──► [web] panel ◄──► MQTT (state in, commands out)
```

- Every component is a small, independent service with one job.
- Services communicate only over MQTT (Mosquitto on the Pi).
- Services are C++ or Python (D5, D15). MQTT topic names are defined once in `shared/topics.json` and used by every service.
- ears and brain must be host-agnostic: broker address comes from config/env, never hard-coded.
- Microphone and speaker are the same USB device (Jabra Speak 510 MS); music comes from the Nests over Bluetooth (D20).

---

## Phase 1 – Voice assistant ✅

Summary of every step, rebuild order, config and troubleshooting: `docs/guides/01-setup-summary.md` (incl. the extras
1.13 Hue lights, 1.14 web panel and 1.15 Nest speakers).
Speech-to-text training: `docs/voice-training.md`.

### Deferred (not needed for Phase 1, pick up when useful)

- **Wake word CPU:** ears idles at ~8 % of one core; `embedding_model.onnx` is 3.7 of 4.2 ms per 80 ms chunk. Options
  that change behaviour: quantized embedding model, or skipping inference during silence. Needs score comparisons on the
  wake-word clips `deploy/record.sh` collects.
- **Next voice session** (decided 2026-10-28): the eval that day showed whisper misses the phrasings and names that are
  rare in the 90 `train` sentences, and 440 re-recordings of them gained nothing. First extend the `train` corpus
  (every verb family and name in `test` 5×, plus commands for new features as they come), then record only the new
  lines, ideally after 2.6 (new mic position). The installed model stays until then.
- **Custom "Hey buddy"** (summary guide 1.6): train with box music in the negatives; the training could move into
  `tools/` like speech-to-text.
- **LLM brain:** implement `IntentParser`; keep the rules as the fast path.
- **Bathroom Bluetooth crackles** (seen 2026-10-30): with a phone between the Pi and the Nest Mini (−69 dBm through
  walls) the music crackles at times. PipeWire already lowers SBC's bitrate on a struggling link; the levers are
  hardware: a USB Bluetooth adapter with an external antenna placed higher (would also allow two Nests at once ⚠️), or
  the Pi's antenna unshielded in the 2.6 body. Quick check first: the phone on 5 GHz Wi-Fi.
- **Persistent journald** (`Storage=persistent`, `SystemMaxUse=50M`) for boot post-mortems.

## Phase 2 – Head & personality

| Step | Summary | Guide |
|---|---|---|
| 2.1 3D printer | Set up the Bambu Lab X2D + AMS. Print tolerance tests (holes, clearances, PETG with PLA support interface) to find CAD offsets. | `docs/guides/02-01-printer.md` |
| 2.2 Motion controller | Pico 2 H, servos on its hardware PWM, separate 5 V servo supply. USB serial link to the Pi, firmware flashed from the Pi (D17). | `docs/guides/02-02-motion-controller.md` |
| 2.3 Motion service | `[motion]`: MQTT commands → Pico serial protocol. | `docs/guides/02-03-motion-service.md` |
| 2.4 Eyes | Round display (e.g. GC9A01) with states: idle, listening, thinking, playing. | `docs/guides/02-04-eyes.md` |
| 2.5 Head pan/tilt | Two servos. Head turns toward the user on wake word. | `docs/guides/02-05-head.md` |
| 2.6 Head & body design | Onshape CAD, print head and body housing the Pi (PETG; PLA only away from heat). Retrain speech-to-text afterwards (the mic sounds different). | `docs/guides/02-06-body-design.md` |

## Phase 3 – Hand

| Step | Summary | Guide |
|---|---|---|
| 3.1 Hand design | Start from the InMoov hand (tendon-driven), remix as needed. | `docs/guides/03-01-hand-design.md` |
| 3.2 Print & assemble | Fingers and palm, tendons, 5 servos. | `docs/guides/03-02-hand-build.md` |
| 3.3 Hand control | Finger positions and gestures in the motion service. | `docs/guides/03-03-hand-control.md` |
| 3.4 Voice gestures | "Hey buddy, wave / high five". | `docs/guides/03-04-gestures.md` |

## Phase 4 – Arm & vision

| Step | Summary | Guide |
|---|---|---|
| 4.1 Arm | 2–3 joints, stronger servos, mounted to the body. | `docs/guides/04-01-arm.md` |
| 4.2 Camera | Pi Camera Module 3, face detection. | `docs/guides/04-02-camera.md` |
| 4.3 Face tracking | Head follows the user. | `docs/guides/04-03-face-tracking.md` |

## Phase 5 – Riftbound

Buddy plays Riftbound against Dev with real cards. Plan, decisions and architecture: `docs/plans/05-riftbound.md` (D21).

| Step | Summary |
|---|---|
| 5.1 Card data & decks | Riftcodex → `~/buddy/data/riftbound/cards.json`, printing code parser, the two decklists |
| 5.2 Rules engine | Core Rules 1.4: turns, runes, units, battlefields, showdowns, combat, chain, scoring; partial information |
| 5.3 Cards | Every card of both decks, one test each |
| 5.4 AI | Heuristic, then ISMCTS with a time budget on the Pi |
| 5.5 `table` + sim | Hand pile model (draw on top, play from the bottom), scan/verify/recover, simulated table with faults |
| 5.6 `game` + panel | MQTT, Riftbound page on the web panel for Dev's moves, voice lines |
| 5.7+ Hardware | After Phase 4: card scanner, hand tray, card placement |

---

## Repo structure

```
buddy/
├── CLAUDE.md                ← this file
├── README.md                ← front page: doc map, everyday commands
├── buddy-hardware.md        ← current hardware & software
├── docs/
│   ├── guides/              ← step guides of the current phase, one summary per finished phase
│   ├── plans/               ← step plans before their guide exists, CAD prompt, Phase 5 plan
│   └── voice-training.md    ← speech-to-text training workflow
├── skeleton/                ← project root (CMake + uv workspace); paths below and in "Dev workflow" are relative to it
│   ├── shared/              ← topics.json (all MQTT topics), C++ helpers, python/buddy_shared
│   ├── third_party/         ← ONNX Runtime, libpiper (prebuilt in /opt), whisper.cpp, cpp-httplib (FetchContent)
│   ├── services/
│   │   ├── ears/            ← wake word, VAD, STT
│   │   ├── brain/           ← intent routing
│   │   ├── voice/           ← TTS
│   │   ├── spotify/         ← Spotify skill, ducking the music box
│   │   ├── hue/             ← Hue lights skill (Hue Bridge, local API)
│   │   ├── web/             ← web panel: HTTP ⇄ MQTT bridge + www/ page
│   │   ├── speakers/        ← Nests over Bluetooth: music and voice routing, ducking (D20)
│   │   └── motion/          ← joints → Pico serial protocol, gestures
│   ├── tools/voice/         ← commands.tsv (corpus) + buddy_voice (record/train/eval, runs on the PC)
│   ├── firmware/pico-motion/ ← Pico 2 firmware: all servos (separate CMake project, D17)
│   └── deploy/              ← PC scripts; pi/ = scripts that run on the Pi; config/ = units, env example, PipeWire
└── parts/                   ← printable parts: STLs, arm/ in parametric OpenSCAD
```

---

## Dev workflow

| Loop | How |
|---|---|
| Inner (try things) | CLion Full Remote Mode → Run/Debug. Syncs to `~/buddy/dev` on the Pi, builds Debug there, runs/debugs there. Stop the deployed unit first (same MQTT client id). |
| Test | `deploy/test.sh` (gtest under ASan/UBSan, pytest, shellcheck); `deploy/test.sh --pi` runs the C++ tests on the Pi |
| Keep | `git commit && git push` (GitHub, public: no secrets, no voice data) |
| Firmware | `deploy/deploy.sh --firmware`: build `firmware/pico-motion` on the Pi, flash the Pico (`picotool load -x -f`) |
| Deploy | `deploy/deploy.sh [service…]` → rsync to `~/buddy/src`, Release build (C++) or uv sync (Python), install to `~/buddy/bin`, restart `buddy@<service>` |
| Check | `deploy/logs.sh <service>`, `deploy/status.sh [-l]`, `ssh buddy "mosquitto_sub -v -t 'buddy/#'"`, panel `http://buddy.local:8080` |
| Try one command | `deploy/run.sh <service> <flags>`: the service's offline mode on the Pi with `buddy.env` loaded (`hue --cmd list`, `brain --test "…"`; examples in `README.md`) |
| Voice training | `deploy/record.sh <label> [test]`, `deploy/train.sh` (`docs/voice-training.md`) |
| Pi setup | `deploy/deploy.sh --setup` (runs `deploy/pi/setup.sh`; single steps: `setup.sh <step>`, `setup.sh check`) |

Pi paths: `~/buddy/{dev,src,build-release,build-pico,bin,venv,models,config,voice}`. Runtime config: `~/buddy/config/buddy.env`
(not in Git; keep only the lines that differ from the defaults). SSH alias `buddy` → `dev@buddy.local`.

## Code conventions

- C++20, CMake + Ninja, `-Wall -Wextra -Wpedantic`. Dependencies from apt where possible.
- Python ≥ 3.12 (Pi: Debian's 3.13), uv, dependencies pinned in `uv.lock`; shared code from `buddy_shared`.
- One executable per service: `services/<name>/` → `buddy-<name>` (C++ target or Python console script) → unit `buddy@<name>`.
- Topics: `buddy/<service>/<name>`, JSON payloads, all in `shared/topics.json`. Events are published only by the owning service, commands consumed only by it.
- Every service: `buddy/<service>/status` = `online`/`offline` (retained, last will). MQTT client id `buddy-<service>`.
- Config only via `BUDDY_*` environment variables, documented in `deploy/config/buddy.env.example`. Never hard-code hosts.
- Log to stdout (journald). Keep logs quiet in steady state: microSD wear.
- New service checklist: folder; C++: `<name>_core` lib (logic) + CMake target with `install(… COMPONENT <name>)` +
  `add_subdirectory` + `buddy_add_tests(<name> tests/…)`; Python: `pyproject.toml` with console script `buddy-<name>` +
  entry in the workspace `members` + `tests/`; topics in `topics.json`; name in `SERVICES` in `deploy.sh` and in
  `logs.sh`; watchdog ping in the main loop (D14); env keys in `buddy.env.example`; on the panel: `SERVICE_CHIPS` in
  `services/web/www/app.js`.

## Golden rules

1. **Never power servos from the Pi.** Always a separate supply with a shared ground.
2. **One service, one job.** Communicate only over MQTT.
3. **Get it working, then make it nice.** Simple rules before LLMs, cardboard before 3D prints.
4. **Update the progress table** whenever a step is done.

---

## How to work

1. Anchor every answer to the current step in the progress table. Don't jump ahead unless asked, but flag decisions that affect later phases (e.g. MQTT topic names that motion will reuse).
2. Search before giving install commands, versions or API details. These change fast: Raspberry Pi OS, PipeWire, openWakeWord, ONNX Runtime, whisper.cpp, Piper / libpiper, CLion, uv, PyTorch, transformers, Spotify Web API (Development Mode limits).
3. Enforce the golden rules. Warn clearly about anything risky: power, wiring, heat, servo current.
4. Small runnable increments. Every step ends with a concrete test matching its "Done when".
5. Measure on real data (Dev's recordings, the Pi) before judging or tuning; state numbers, not impressions.
6. State assumptions. Ask at most one question when blocked.

### Pi 5 specifics

- aarch64: check that every library/binary has an arm64 build.
- Audio goes through PipeWire.
- The 27 W supply is required for the full 1.6 A USB current budget.
- The Active Cooler is fitted permanently; don't remove it.
- Never pull power while the OS runs (`sudo poweroff` first): risk of microSD corruption.

---

## Output formats

- **Step guides** (`docs/guides/XX-YY-name.md`): Goal → Prerequisites → Parts/Tools → Steps (copy-pasteable commands with expected output) → Verify → Troubleshooting → Notes for later phases. Mark untested assumptions with ⚠️.
- **Code:** C++ with CMake, or Python with uv (D5). MQTT topics live in `shared/topics.json`. Broker address and other host-specific values come from config/env.
- **CAD/printing** (Onshape or parametric OpenSCAD, Bambu X2D + AMS): dimensions with clearance offsets, print orientation, supports.
  Filament: PETG for structure and every part that carries load, sits near heat or has a fit; PLA only for support interfaces under PETG (second nozzle), jigs, mock-ups and cosmetic parts away from heat. No TPU.
- **Style:** concise, no filler. Tables for BOMs and pinouts, code blocks for commands.

## Maintenance

- When a step is done: output the updated progress-table row(s) for this file.
- When hardware changes: output the updated lines for `buddy-hardware.md`.
- When a decision changes: output the updated row of the Decisions table.
- When a phase is done: condense its guides into `docs/guides/0N-setup-summary.md`.
