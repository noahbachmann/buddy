# Buddy – Desk Robot

Stationary desk robot: a voice assistant first ("Hey buddy, play Spotify"), now a Riftbound opponent that handles its own
real cards: card dispensers next to the play mat and a "hand" that moves over the mat and docks onto them.

This file is the single source of truth for roadmap, architecture, decisions, progress and working rules.

## Files

| File | Purpose |
|---|---|
| `CLAUDE.md` | Roadmap, architecture, decisions, progress, open work, rules (this file) |
| `README.md` | Front page for Dev: what Buddy is, where to read what, everyday commands (`deploy/run.sh` examples) |
| `buddy-hardware.md` | Current parts and tools |
| `docs/01-voice.md` | Phase 1 condensed: rebuild, config, every voice feature (1.2–1.17), troubleshooting |
| `docs/02-riftbound/design.md` | Riftbound software: decisions, pile model, architecture, each step's design and as built |
| `docs/motion-controller/` | Side project: Pico 2 + servos (`pico.md`, wiring sheet `pico-setup.html`), motion service (`service.md`) |
| `docs/voice-training.md` | Recurring workflow: record, train, score and install speech-to-text on Dev's voice |
| `docs/3d-printing.md` | Living printing reference: materials, slicer baseline, design rules, measured tolerances, troubleshooting, print log |
| `software/CLAUDE.md` | Code quick reference: service pattern (C++ and Python), shared helpers, deploy scripts, tests |
| `software/services/web/CLAUDE.md` | Web panel internals: server, state stream, `www/` page structure |
| `software/services/game/CLAUDE.md` | Riftbound engine as it is now: the loop, hidden information, hook points for cards, invariants, test helpers, gotchas |
| `software/services/table/CLAUDE.md` | Buddy's physical cards (2.5): tracker procedures, sim faults, soak, gotchas |
| `software/services/screener/CLAUDE.md` | The screener service (#66): screen model, navigator, fixtures, measured gotchas |
| `software/android/screener-ime/CLAUDE.md` | The screener phone's keyboard (#66): adb commands, build and install, gotchas |
| `parts/CLAUDE.md` | CAD toolchain (pinned OpenSCAD nightly, BOSL2, NopSCADlib, Python checks), conventions for printable parts |

- Read this file and `buddy-hardware.md` before answering about setup, parts or next steps.
- The progress table is the current status.
- Work only Dev can do (recordings, prints, hardware tests) goes into the phase's issue as checkboxes; docs describe
  the state after it.
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
| 1 – Voice (extra) | 1.16 messenger: Buddy's notices and questions in Dev's Telegram chat (D22, #68): done 2026-11-15: paired, verified on the Pi (notice, button answer across a restart, offline → back online alert) | ✅ |
| 1 – Voice (extra) | 1.17 system monitor: host metrics (CPU, RAM, disk, temp, fan, net, throttle flags) as per-host cards on the panel (D23, #70): done 2026-11-16 | ✅ |
| 2 – Riftbound | Software against a simulated table (#18, design: `docs/02-riftbound/design.md`): 2.1 card data & decks, 2.2 rules engine, 2.3 cards of both decks, 2.4 AI (greedy + ISMCTS, 2 s on 3 threads on the Pi), 2.5 `table` + sim (soak: 0 unrecovered), 2.6 `game` service + panel page (#46). Done 2026-11-28: Dev played a full live game with real cards on the panel (#53, Dev won 8–4 in 13 turns) | ✅ |
| 3 – Hand dispenser | Hand pile + graveyard on one dispenser, a hold on top swaps the stacks (#93). `parts/dispenser`: roller, lift, pusher designed; plates being printed | 🟨 |
| 4 – Deck dispenser | Draw pile, inserts on top and bottom for recycled cards (#94) | ⬜ |
| 5 – Playfield & hand | A machine that moves over the mat and docks onto the dispensers; runes in it or in a small dispenser (#95) | ⬜ |
| Side projects | Motion controller (Pico 2 + servos, motion service: code deployed, wiring and servo test open, `docs/motion-controller/`, D17) · screener (#66) · PC case (#87) | 🟨 |

Legend: ⬜ not started · 🟨 in progress · ✅ done

## Open work

What's left right now, in order. Update it with the progress table; GitHub issues hold the details of code work.

**Dev** (needs the hardware, the voice or the room):

1. **Dispenser prints and assembly**: the checkboxes in #93.
2. **Live check** for #12: "Hey Jarvis, play Spotify" ducks, plays, and Buddy answers without delay (idle loops changed).
3. Side projects when wanted: screener to-dos in #66; motion controller wiring (`docs/motion-controller/pico.md` steps
   1–3, 9, then `service.md` Verify).

**Claude** (code and guides):

1. Phase 3 (#93): dispenser changes from Dev's prints; then the electronics and the `table` hardware backend.
2. #13 voice seek ("fast-forward 30 seconds", "go back 2 minutes").

Phase 1 → Deferred lists what waits for a reason (wake word CPU, next voice session, custom "Hey buddy", LLM brain).

---

## Decisions

| ID | Topic | Decision | Status |
|---|---|---|---|
| D1 | OS | Raspberry Pi OS Lite 64-bit (Debian 13 "Trixie"), headless | ✅ |
| D2 | Message bus | Mosquitto on the Pi. Services talk only over MQTT. | ✅ |
| D3 | Compute placement | ears and brain run on the Pi *or* a LAN host. Only the broker address changes. | ✅ |
| D4 | Storage | microSD only, no NVMe | ✅ |
| D5 | Languages | C++ (CMake) by default, for services and the Pico firmware. Python where its ecosystem matters: PC tooling and training (`software/tools/`), and Pi services that need Python libraries (LLM, vision later), with the same conventions as C++ services (D15). No Kotlin or Java, except Android code on the screener phone (`software/android/`, Kotlin): Android requires it (2026-11-16, #66). | ✅ |
| D6 | Dev workflow | CLion on the PC, Full Remote Mode over SSH: sources stay local (and on GitHub), CLion syncs them to the Pi, builds with the Pi's CMake/GCC, runs and debugs with the Pi's GDB. | ✅ |
| D7 | Runtime packaging | systemd user units on the Pi (`buddy@<service>`, lingering on), Mosquitto from apt. Docker only for services offloaded to a LAN host later. | ✅ |
| D8 | Audio capture | ears reads the raw Jabra mic via ALSA `default` (PipeWire's default source, no echo cancellation: D13), 16 kHz mono s16. Wake word, VAD and STT live in one service (`ears`) sharing that stream. | ✅ |
| D9 | ML libraries | ONNX Runtime: Microsoft's prebuilt aarch64 release in `/opt/onnxruntime` (Debian's package spams the log). whisper.cpp: pinned tag built via CMake FetchContent. Models in `~/buddy/models` via `deploy/pi/fetch-models.sh` (what `buddy.env` names), not in Git; own models (fine-tuned whisper `ggml-buddy-*`) come from the PC. | ✅ |
| D10 | Spotify | Music never through the Jabra. Default speaker `BUDDY_SPOTIFY_DEVICE` = the Main speaker Nest (D20). Speakers by name ("… on the bathroom speaker"; brain splits off the name, spotify matches it against the device list): the Nests plus whatever Spotify sees (DESKTOP-1, Pixel 8); pause/next/volume follow the device that is playing. The web panel shows and controls whatever plays on any device and moves it between speakers (by device id; polled every 3 s only while a panel is open). spotify controls them via the Web API (libcurl, OAuth PKCE, refresh token in `~/buddy/config/spotify-token.json`). Spotify Soloist on the Pi plays for the Nests (back 2026-10-30, D20). | ✅ |
| D11 | Intents & replies | brain: regex rules behind `IntentParser` (LLM-replaceable), routes to `buddy/<skill>/cmd`. `software/tools/voice/commands.tsv` is the phrasing it must understand (ctest). The service that knows the outcome publishes `buddy/voice/say`. | ✅ |
| D12 | TTS | libpiper v1.8.0 built from source by `setup.sh` into `/opt/libpiper` (against `/opt/onnxruntime`), patched to read its ONNX thread count from `PIPER_INTRA_OP_THREADS` (stock: 1; voice sets 3 via `BUDDY_VOICE_THREADS`, first audio ~830 → ~450 ms); voice `en_GB-alan-medium`. | ✅ |
| D13 | Background music, no echo cancellation | The box's music is room noise to Buddy. While ears listens or voice speaks the music is ducked: on a Nest by speakers on the Pi (`buddy_music`, D20), on any other device in the room by spotify via the Web API. **No echo cancellation** (removed 2026-10-28): on the same test session the model trained on the raw mic beat the echo-cancelled one (intent 50 vs 47 of 59, WER 7.4 vs 9.5 %), and its only job, removing Buddy's own voice, is covered by ears ignoring the wake word while voice speaks. Stock PipeWire, training on the raw track. | ✅ |
| D14 | Recovery | `buddy@.service` is `Type=notify` with `WatchdogSec=60` (READY from `Mqtt::start`, pings from each main loop, `shared/systemd.*` / `buddy_shared.systemd`, no libsystemd). Crashes restart with backoff 1 → 30 s and never give up. No core files. | ✅ |
| D15 | Python services | uv workspace in `software/` (`pyproject.toml` + `uv.lock`, members listed explicitly). `shared/python/buddy_shared` mirrors the C++ shared helpers; `shared/topics.json` is the single topic list (CMake generates `topics.hpp`). On the Pi: uv in `~/.local/bin` (pinned), Debian's Python, one venv `~/buddy/venv`, console script linked into `~/buddy/bin`, same unit template. | ✅ |
| D16 | Voice data | Recordings are personal data and the repo is public: never in Git. Master copy on Dev's PC in `~/buddy-voice` (backed up); the Pi keeps the recorded sessions too (pull copies, never deletes), not the labelled live clips. Training runs on Dev's PC (NVIDIA GPU, CUDA 12.8 PyTorch). | ✅ |
| D17 | Motion controller (side project) | Raspberry Pi Pico 2 H on the Pi's USB, servos on its GPIO hardware PWM (50 Hz, 16 outputs), no PCA9685. Firmware C++ with Pico SDK 2.3.1 (FetchContent) in `software/firmware/pico-motion` (one folder per Pico role: `pico-<job>`; this one drives every servo), built and flashed on the Pi (`deploy.sh --firmware`, picotool 2.3.1 in `/opt/picotool`). Text protocol v1 over USB CDC (`/dev/buddy-pico`), header shared with motion. The Pico enforces per-channel pulse limits and relaxes all servos 2 s after the last command. Servo power: separate 5 V supply, shared ground, never via the Pi or the Pico's VBUS/VSYS. | ✅ |

| D18 | Lights | Philips Hue via the Hue Bridge's local CLIP v2 API (no cloud, no Hue account): service `hue`. TLS verified against Hue's `root-bridge` CA (compiled in) with the bridge id as host name (`CURLOPT_RESOLVE` → bridge IP). Application key from `buddy-hue --pair` (link button) in `~/buddy/config/hue.json`. Live state from the bridge's eventstream → retained `buddy/hue/state`. Hue Bridge Pro would need its newer CA ⚠️. | ✅ |
| D20 | Nest speakers | The two Nests play over **Bluetooth A2DP** (SBC) from the Pi: Spotify Soloist ("Buddy", hidden; sandboxed, weekly build update) → PipeWire sink `buddy_music` → the selected Nest; Buddy's voice → `buddy_voice` → that Nest while music plays there (60 %), else the Jabra. Service `speakers` (C++, `pactl`/`pw-link`/`bluetoothctl`, event-driven): connects on demand, one Nest at a time (the Pi refuses a second A2DP link), disconnects after 10 min without music, ducks `buddy_music` locally. spotify lists the Nests (`bt:<mac>`) in Soloist's place. Not Cast: Buddy can't start Spotify on an idle Nest (403 without Spotify's own desktop login), Soloist can't cast, Cast streaming lags ≥ 3.9 s (#16). | ✅ |
| D19 | Web panel | Service `web` (C++, cpp-httplib pinned via FetchContent): static page + HTTP⇄MQTT bridge, no skill logic. Retained `buddy/+/state` and `/status` → browser (server-sent events); `POST /api/cmd/<svc>` → `buddy/<svc>/cmd` (non-planned command topics only). `http://buddy.local:8080`, LAN only, **no login**: Host allowlist (IP, `*.local`, `BUDDY_WEB_HOSTS`) against DNS rebinding, JSON-only same-origin POSTs against other sites in the same browser. Add auth before anything that moves (motion) goes on the page: `"web": false` in topics.json keeps `buddy/motion/cmd` off it. Riftbound (`#riftbound`, 2.6): `buddy/game/cmd` is on the page without a login while the table is a sim; a login comes before the table drives hardware (Phase 3). | ✅ |
| D21 | Card game | Riftbound (Phase 2): **own C++ rules engine, no LLM**: code is the referee, search (heuristic, then ISMCTS over Dev's hidden cards) picks the plays. alpharune is reading material only (no license). Cards scoped to two precons (Vi, Unleashed; Zed, Vendetta); an unimplemented card is a hard error. House rule: each player picks their Chosen Champion per game from the legend's champion units (#98; Buddy by greedy self-play). Rules: Core Rules 2026-10-09 (the Rules Hub serves only the current document). Card data from the Riftcodex API, fetched on the Pi. Services `game` (rules, AI, which cards are in hand) and `table` (physical piles, hand order, scan/recover; sim backend now, the dispensers and the playfield machine from Phase 3). | ✅ |
| D22 | Reaching Dev | Service `messenger` (C++, Telegram Bot API over libcurl): notices and questions from any service go to Dev's private **Telegram** chat with Buddy's own bot, answers come back on the bus (`buddy/messenger/cmd` → `buddy/messenger/event`). Named after the job: askers never see the vendor. Outbound HTTPS only (long poll; no open port, no webhook). Only the paired account's private chat is heard, everyone else gets no answer. Bot chats are **not end-to-end encrypted**: a service sends only what may sit on Telegram's servers. `cmd` is `"web": false` and `state` carries counts only (the panel has no login, D19); message texts never reach the journal. Bot token + chat from `buddy-messenger --pair` in `~/buddy/config/messenger.json`. A built-in watcher reports services that stay offline or keep restarting. Email dropped: replies need IMAP + quote parsing, Proton has IMAP only through its Bridge and SMTP tokens only for custom domains. "What's the status" / `/status` in the chat answers with one line per host (from `buddy/system*/state`, D23) plus the service watcher summary; other commands to Buddy from the chat ("lights off"): later. | ✅ |

| D23 | Host monitoring | Own service `system` (C++, /proc + /sys + `vcgencmd get_throttled`), no node_exporter/Prometheus/Grafana: retained `buddy/system/state` every 5 s (published only on change), card per host on the panel, live values only. Extra hosts set `BUDDY_SYSTEM_NAME` → service `system-<name>` with its own client id and `buddy/system-<name>/{state,status}`, so everything (web's `buddy/+/state`, status, last will, messenger watcher) works unchanged. Dashboard-only; metric alerting (temp, disk) deliberately left to a later watcher. | ✅ |

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
   [voice] TTS     [spotify] skill ──Web API──►  [hue] skill ──LAN──►  [motion] ──USB──► Pico (servos, side project)
        │           Spotify devices; "Buddy" =     Hue Bridge
        │           Soloist on the Pi              (eventstream back)
        ▼                │ music
   buddy_voice      buddy_music ──► [speakers] ──Bluetooth──► Nest (on demand, music ducked while Buddy listens/speaks)
        └──────────────────────────► [speakers] ──► that Nest while music plays there, else the Jabra speaker
                                        phone / PC browser ◄──HTTP──► [web] panel ◄──► MQTT (state in, commands out)
        any service ──MQTT──► [messenger] ──HTTPS──► Telegram ──► Dev's phone (notices; questions, answers back on MQTT)

 Riftbound:  panel (Dev's moves) ──► [game] rules + AI ──► [table] piles, hand order ──► sim now; dispensers + hand (Phases 3–5)
```

- Every component is a small, independent service with one job.
- Services communicate only over MQTT (Mosquitto on the Pi).
- Services are C++ or Python (D5, D15). MQTT topic names are defined once in `shared/topics.json` and used by every service.
- ears and brain must be host-agnostic: broker address comes from config/env, never hard-coded.
- Microphone and speaker are the same USB device (Jabra Speak 510 MS); music comes from the Nests over Bluetooth (D20).

---

## Phase 1 – Voice assistant ✅

Summary of every step, rebuild order, config and troubleshooting: `docs/01-voice.md` (incl. the extras
1.13 Hue lights, 1.14 web panel, 1.15 Nest speakers, 1.16 messenger and 1.17 system monitor).
Speech-to-text training: `docs/voice-training.md`.

### Deferred (not needed for Phase 1, pick up when useful)

- **Wake word CPU:** ears idles at ~8 % of one core; `embedding_model.onnx` is 3.7 of 4.2 ms per 80 ms chunk.
  Quantizing it is **rejected on data** (2026-11-16, #72, scored on the 100 recorded "Hey Jarvis" clips + 419 command
  clips as negatives): dynamic int8 61/100 detections vs 67/100 fp32 and slower (ConvInteger), static QDQ int8 65/100
  with one 0.99 false positive. Longer strides break the classifier (trained on the 80 ms feature hop: scores → 0).
  Remaining option: skip embedding inference while the room is below ears' level gate: only pays while no music plays,
  needs a live A/B with the adaptive gate.
- **Next voice session**: #67 (done 2026-11-17) grew the corpus (every `test` word 5× in `train`, Dev's short forms
  from live use) and retrained: intent 67 → 87 / 89, WER 12.5 → 1.2 % (summary 1.7). Open: whisper heard "switch
  all" as "switch on" before the last test session was re-recorded; if it comes back, a brain guard ("switch on/one
  lights off" = all) was weighed and left out (it would be tuned on test clips). Ongoing: `train.sh history` every
  ~100 live commands, retrain at a few hundred. After the Jabra moves: a fresh test session first.
- **Custom "Hey buddy"** (summary guide 1.6): train with box music in the negatives; the training could move into
  `tools/` like speech-to-text.
- **LLM brain:** implement `IntentParser`; keep the rules as the fast path.
- **Bathroom Bluetooth crackles** (seen 2026-10-30): with a phone between the Pi and the Nest Mini (−69 dBm through
  walls) the music crackles at times. PipeWire already lowers SBC's bitrate on a struggling link; the levers are
  hardware: a USB Bluetooth adapter with an external antenna placed higher (would also allow two Nests at once ⚠️), or
  the Pi's antenna unshielded in a future housing. Quick check first: the phone on 5 GHz Wi-Fi.
- **Persistent journald** (`Storage=persistent`, `SystemMaxUse=50M`) for boot post-mortems.

## Phase 2 – Riftbound ✅

Buddy plays Riftbound against Dev with real cards (D21). Software first, against a simulated table (#18). Design and
as built: `docs/02-riftbound/design.md`; how the code runs: `software/services/{game,table}/CLAUDE.md`.

| Step | Summary |
|---|---|
| 2.1 Card data & decks | Riftcodex → `~/buddy/data/riftbound/cards.json`, printing code parser, the two decklists |
| 2.2 Rules engine | Core Rules 2026-10-09: turns, runes, units, battlefields, showdowns, combat, chain, scoring; partial information |
| 2.3 Cards | Every card of both decks, one test each |
| 2.4 AI | Heuristic, then ISMCTS with a time budget on the Pi |
| 2.5 `table` + sim | Hand pile model (draw on top, play from the bottom), scan/verify/recover, simulated table with faults |
| 2.6 `game` + panel | MQTT, Riftbound page on the web panel for Dev's moves, voice lines |

## Phases 3–5 – The table hardware

Dispensers beside the mat hold Buddy's piles; one "hand" moves over the mat and docks onto them. Each phase's plan
and status live in its issue.

```
┌─────────────┬─────────────┐
│  mat        │  mat        │
│        [hand]             │┌───────────────┐
├─────────────┼─────────────┤│ hand/graveyard│  Phase 3 (#93)
│  mat        │  mat        │├───────────────┤
│             │             ││ deck          │  Phase 4 (#94)
└─────────────┴─────────────┘└───────────────┘
   hand: moves anywhere on the mat, docks onto the dispensers (Phase 5, #95)
```

| Phase | What |
|---|---|
| 3 Hand dispenser | `parts/dispenser`: hand pile on a belt roller over the camera, a lift and a pusher; the graveyard on the same dispenser, a hold on top swaps the two stacks |
| 4 Deck dispenser | Draw pile; cards also go back in (recycling), so inserts on top and bottom |
| 5 Playfield & hand | Moves over the mat, takes cards from the dispensers, places / exhausts them; runes in a compartment or a small dispenser |

---

## Repo structure

```
buddy/
├── CLAUDE.md                ← this file
├── README.md                ← front page: doc map, everyday commands
├── buddy-hardware.md        ← current hardware & software
├── docs/
│   ├── 01-voice.md          ← Phase 1 condensed: rebuild, config, troubleshooting
│   ├── 02-riftbound/        ← Riftbound design + as built
│   ├── motion-controller/   ← side project: Pico 2 + servos, motion service
│   ├── voice-training.md    ← speech-to-text training workflow
│   └── 3d-printing.md       ← printing reference + print log (grows with every print)
├── software/                ← project root (CMake + uv workspace); paths below and in "Dev workflow" are relative to it
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
│   │   ├── messenger/       ← Dev's Telegram chat: notices, questions and answers, service watcher (D22)
│   │   ├── system/          ← host metrics for the panel: CPU, RAM, disk, temp, fan, net, throttling (D23)
│   │   ├── screener/        ← Python: a social app on a dedicated phone over adb (#66, side service)
│   │   ├── motion/          ← joints → Pico serial protocol, gestures (side project)
│   │   ├── table/           ← Riftbound: Buddy's physical cards: piles, hand tracker, sim backend, soak (2.5)
│   │   └── game/            ← Riftbound: riftbound/ lib (cards, decks, rules, AI), table moves, decks/ (Phase 2)
│   ├── tools/voice/         ← commands.tsv (corpus) + buddy_voice (record/train/eval, runs on the PC)
│   ├── firmware/pico-motion/ ← Pico 2 firmware: all servos (separate CMake project, D17)
│   ├── android/screener-ime/ ← keyboard on the screener phone: types and reads back text (Gradle project, Kotlin, #66)
│   └── deploy/              ← PC scripts; pi/ = scripts that run on the Pi; config/ = units, env example, PipeWire
└── parts/                   ← printable parts: one folder per module (dispenser/, pc-case/, hex-driver/): print plates at its
                                root, stl/ single parts, src/ OpenSCAD; lib/ (BOSL2, NopSCADlib submodules), tools/ (cad CLI, #54)
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
| Voice training | `deploy/record.sh <label> [test\|new\|test-new]`, `deploy/train.sh [history]` (`docs/voice-training.md`) |
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

## Docs

- `docs/` explains what exists. Plans, to-dos and status live in GitHub issues (checkboxes; open/closed = where we
  are), never in docs. Phase work: one epic issue per phase, sub-issues per piece.
- One folder per phase (`02-riftbound/`, numbered as the progress table) or side project (`motion-controller/`,
  unnumbered). A topic with a single doc is one file in `docs/` (`voice-training.md`, `01-voice.md`).
- A module's how-it-works doc sits next to its code (`parts/<module>/README.md`, service `CLAUDE.md`).
- Obsolete docs are deleted, never archived (Git keeps them). Temporary guides for Dev go when done.

## Golden rules

1. **Never power servos from the Pi.** Always a separate supply with a shared ground.
2. **One service, one job.** Communicate only over MQTT.
3. **Get it working, then make it nice.** Simple rules before LLMs, cardboard before 3D prints.
4. **Update the progress table** whenever a step is done.

---

## How to work

1. Anchor every answer to the current step in the progress table. Don't jump ahead unless asked, but flag decisions that affect later phases (e.g. table commands the dispensers will execute).
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

- **Guides** (in the phase's or side project's `docs/` folder): Goal → Prerequisites → Parts/Tools → Steps (copy-pasteable commands with expected output) → Verify → Troubleshooting → Notes for later phases. Mark untested assumptions with ⚠️.
- **Code:** C++ with CMake, or Python with uv (D5). MQTT topics live in `shared/topics.json`. Broker address and other host-specific values come from config/env.
- **CAD/printing** (Onshape or parametric OpenSCAD, Bambu X2D + AMS): dimensions with clearance offsets, print orientation, supports.
  Filament: PETG for structure, real load (brackets, motor mounts, carriages) and anything near heat (motors, Pi, drivers); PLA for low-load functional parts away from heat, where its stiffness and accuracy help (fits, sliding edges, e.g. the dispenser's lift and pusher), plus support interfaces under PETG (second nozzle), jigs, mock-ups and cosmetic parts. PLA softens from ~55 °C: never touching a motor body; within a few mm of one only if the motor runs at moderate current with a low hold current (e.g. the pusher at home). No TPU.
- **Style:** concise, no filler. Tables for BOMs and pinouts, code blocks for commands.

## Maintenance

- When a step is done: output the updated progress-table row(s) for this file.
- When hardware changes: output the updated lines for `buddy-hardware.md`.
- When a decision changes: output the updated row of the Decisions table.
- When a phase is done: condense its docs into one `docs/0N-<name>.md` (like `01-voice.md`) if that's easier to use.
