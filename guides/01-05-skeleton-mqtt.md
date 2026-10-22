# 1.5 – Project skeleton, MQTT & dev workflow

> Written 2026-10-19. The skeleton's topic matcher and config code were compile- and unit-tested; the Paho/MQTT parts, CMake config and scripts were **not** run on a Pi yet. Items marked ⚠️ are assumptions.

## Goal

A C++ monorepo in Gitea, Mosquitto running on the Pi, and a fixed edit → build → deploy → test loop:

- **Inner loop (minutes):** edit in CLion on the PC, press Run/Debug, CLion syncs and builds on the Pi and runs the program there with output and breakpoints in CLion.
- **Deploy loop:** `deploy/deploy.sh <service>` builds a Release on the Pi, installs it and (re)starts it as a systemd user service that survives reboots.

Two dummy services (`ping`, `pong`) prove the bus works.

## Prerequisites

- Step 1.3 done, including step 10 (toolchain + `rsync` on the Pi): `ssh buddy` works with your key.
- Step 1.4 (audio) is **not** needed for this step.
- PC: CLion, Git, `rsync`, `ssh`. ⚠️ `deploy.sh` is a bash script: fine on Linux/macOS; on Windows run it from WSL.
- A Gitea account/org where the `buddy` repo will live.

## Parts / tools

| Tool | Where | Role |
|---|---|---|
| CLion (Full Remote Mode) | PC | Editor, run, debug |
| GCC 14, CMake, Ninja, GDB, ccache ⚠️ versions | Pi | Build and debug |
| Mosquitto + `mosquitto-clients` | Pi (apt) | MQTT broker + CLI test tools |
| Eclipse Paho MQTT C++ (`libpaho-mqttpp-dev`) | Pi (apt) | MQTT client library. ⚠️ Debian 13 ships 1.5.x |
| nlohmann/json (`nlohmann-json3-dev`) | Pi (apt) | JSON payloads |
| systemd user units + lingering | Pi | Runs services, restarts them, starts them at boot |
| `buddy-skeleton.zip` | PC | Starting repo for this step |

## Decisions made in this step

| Topic | Choice | Why |
|---|---|---|
| Language level | C++20, `-Wall -Wextra -Wpedantic` | GCC 14 on Debian 13 supports it fully |
| Build | One top-level CMake project, Ninja, ccache | CLion opens it as one project; ccache keeps rebuilds on the Pi fast |
| Dependencies | From apt where possible | Matching arm64 builds, no extra package manager. Bigger libraries (whisper.cpp, ONNX Runtime, libpiper) get decided in their steps. |
| Topic scheme | `buddy/<service>/<name>`, payload JSON | Defined once in `shared/include/buddy/topics.hpp` |
| Availability | `buddy/<service>/status` = `online` / `offline`, retained, `offline` as last will | Any service (and later the eyes) can see who's alive |
| MQTT client id | `buddy-<service>` | Prevents two copies of the same service running at once |
| Config | Environment variables from `~/buddy/config/buddy.env` | Moving ears/brain to another host = change `BUDDY_MQTT_HOST` only (D3) |
| Services | systemd **user** units (`buddy@<name>`), lingering on | Audio lives in the user's PipeWire session; lingering starts them at boot without a login |
| Mosquitto | Localhost only, no auth (for now) | Nothing outside the Pi needs it yet. LAN access + password comes with offloading (see Notes). |

### Topic contract (from `topics.hpp`)

```cpp
// Scheme: buddy/<service>/<name>
//   status   – "online" / "offline", retained, "offline" is the last will (every service)
//   events   – something happened or a state changed; published only by the owning service
//   commands – something should happen; consumed only by the owning service
inline constexpr std::string_view dev_ping        = "buddy/dev/ping";         // event (1.5 test)
inline constexpr std::string_view ears_wake       = "buddy/ears/wake";        // event
inline constexpr std::string_view ears_transcript = "buddy/ears/transcript";  // event
inline constexpr std::string_view brain_intent    = "buddy/brain/intent";     // event
inline constexpr std::string_view voice_say       = "buddy/voice/say";        // command
inline constexpr std::string_view voice_state     = "buddy/voice/state";      // event
inline constexpr std::string_view spotify_cmd     = "buddy/spotify/cmd";      // command
inline constexpr std::string_view spotify_state   = "buddy/spotify/state";    // event
inline constexpr std::string_view motion_cmd      = "buddy/motion/cmd";       // command (phase 2)
inline constexpr std::string_view motion_state    = "buddy/motion/state";     // event   (phase 2)
```

### Where things live on the Pi

| Path | Content |
|---|---|
| `~/buddy/dev/` | CLion's synced copy + its Debug build |
| `~/buddy/src/` | `deploy.sh`'s synced copy |
| `~/buddy/build-release/` | Release build (ccache) |
| `~/buddy/bin/` | Installed binaries (`buddy-ping`, `buddy-pong`, …) |
| `~/buddy/config/buddy.env` | Runtime config, mode 600, not in Git |
| `~/.config/systemd/user/buddy@.service` | Unit template for all services |

## The workflow

```
 CLion on PC ──edit──► Run ▶ / Debug 🐞
     │                    └─ syncs to ~/buddy/dev, builds Debug on the Pi, runs there,
     │                       output + breakpoints in CLion
     │
 git commit && git push ──► Gitea (history, backup)
     │
 deploy/deploy.sh pong ──► rsync → ~/buddy/src → Release build → ~/buddy/bin
     │                      → systemctl --user restart buddy@pong
     │
 Test:  deploy/logs.sh pong            (service log)
        ssh buddy "mosquitto_sub -v -t 'buddy/#'"   (everything on the bus)
```

"Uploading" is always one of the two syncs: CLion's (for trying things) or `deploy.sh` (for keeping them). The Pi never needs to be unplugged or connected by cable; everything goes over SSH.

## Steps

### 1. Create the repo (PC)

```bash
unzip buddy-skeleton.zip            # creates ./buddy
cd buddy
# move the project docs in
mv /path/to/CLAUDE.md /path/to/buddy-hardware.md .
mkdir -p guides && mv /path/to/guides/*.md guides/
git init -b main
git add . && git commit -m "Skeleton: C++ monorepo, MQTT wrapper, ping/pong, deploy scripts"
git remote add origin <your-gitea-url>/buddy.git
git push -u origin main
```

Skeleton contents:

| Path | What it does |
|---|---|
| `CMakeLists.txt` | Top-level project, C++20, finds Paho + nlohmann_json |
| `shared/include/buddy/topics.hpp` | All topic names + wildcard matcher |
| `shared/include/buddy/config.hpp` | Reads `BUDDY_MQTT_*` env vars |
| `shared/include/buddy/mqtt.hpp` | Wrapper: auto-reconnect, last will, online status, resubscribe |
| `shared/include/buddy/lifecycle.hpp` | Clean shutdown on Ctrl+C / SIGTERM |
| `services/ping/` | Publishes `{"n":…,"ts_ms":…}` every second |
| `services/pong/` | Prints each ping with latency, and every status change |
| `deploy/systemd/buddy@.service` | Unit template (`buddy@pong` → `~/buddy/bin/buddy-pong`) |
| `deploy/setup-pi.sh` | One-time Pi setup (packages, Mosquitto, lingering, folders) |
| `deploy/deploy.sh` | Sync, build, install, restart |
| `deploy/logs.sh` | Follow one service's log |

### 2. One-time Pi setup (PC terminal, repo root)

```bash
deploy/deploy.sh --setup
```

It syncs the repo, runs `setup-pi.sh` on the Pi (asks for your sudo password), then builds and starts `ping` and `pong`. Expected ending:

```
→ build, install, restart: ping pong
[n/n] Linking CXX executable services/pong/buddy-pong
  buddy@ping       active
  buddy@pong       active
✓ deployed. Logs: deploy/logs.sh <service>
```

Check the pieces:

```bash
ssh buddy 'systemctl is-active mosquitto'                  # active
ssh buddy 'loginctl show-user "$USER" -p Linger'           # Linger=yes
ssh buddy "dpkg -l libpaho-mqttpp-dev | tail -1"           # ii  libpaho-mqttpp-dev  1.5.x ...  ⚠️
```

### 3. First test: does pong hear ping?

```bash
deploy/logs.sh pong
```

Expected (Ctrl+C to leave):

```
[pong] connected to tcp://localhost:1883
[pong] buddy/ping/status = online
[pong] buddy/pong/status = online
[pong] got #12, latency 1 ms
[pong] got #13, latency 0 ms
```

And the raw bus:

```bash
ssh buddy "mosquitto_sub -v -t 'buddy/#'"
```

```
buddy/ping/status online
buddy/pong/status online
buddy/dev/ping {"n":20,"ts_ms":1760000000000}
```

### 4. Set up CLion Full Remote Mode (PC)

⚠️ Menu names as of recent CLion versions; they may differ slightly.

1. **Open** the `buddy` folder in CLion. It detects the CMake project.
2. **Toolchain:** Settings → Build, Execution, Deployment → Toolchains → `+` → **Remote Host**.
   - Name: `buddy-pi`
   - Credentials: ⚙ → `+` → Host `buddy.local`, port 22, user `dev`, authentication **OpenSSH config and authentication agent** (or *Key pair* → `~/.ssh/id_ed25519`). Test Connection.
   - CLion then detects CMake, GCC and GDB on the Pi. All should get a green check.
3. **CMake profile:** Settings → Build, Execution, Deployment → CMake → Debug profile:
   - Toolchain: `buddy-pi`
   - Generator: Ninja
   - CMake options: `-DCMAKE_CXX_COMPILER_LAUNCHER=ccache`
4. **Sync folder:** Settings → Build, Execution, Deployment → Deployment → the SFTP entry CLion created for `buddy-pi` → Mappings → Deployment path: `/home/dev/buddy/dev`.
   *Why:* CLion's default is a folder under `/tmp`, which on Debian 13 is a RAM disk and is wiped on every reboot (= full rebuild each time). A fixed folder also keeps CLion's copy separate from `deploy.sh`'s `~/buddy/src`.
5. File → Reload CMake Project. CLion syncs the sources and runs CMake on the Pi. The run configurations `buddy-ping` and `buddy-pong` appear.

### 5. Try the inner loop

1. Stop the deployed copy first. Only one `buddy-pong` may be connected at a time (same client id):
   ```bash
   ssh buddy systemctl --user stop buddy@pong
   ```
2. In `services/pong/main.cpp`, change `"[pong] got #"` to `"[pong] received #"`.
3. Select `buddy-pong` and press **Run**. Expected in CLion's console: `[pong] received #… , latency … ms`.
4. Set a breakpoint on that line and press **Debug**. Execution stops on the Pi; inspect `msg` in CLion. Resume, then stop.
5. Keep it: commit, push, deploy just that service:
   ```bash
   git commit -am "pong: wording" && git push
   deploy/deploy.sh pong
   deploy/logs.sh pong
   ```

Optional: add `deploy/deploy.sh` as an External Tool in CLion (Settings → Tools → External Tools) so deploying is a menu click.

## Verify

Run these with `deploy/logs.sh pong` open in a second terminal.

| Test | Command (PC) | Expected in pong's log |
|---|---|---|
| Clean stop | `ssh buddy systemctl --user stop buddy@ping` | `buddy/ping/status = offline`, pings stop |
| Start again | `ssh buddy systemctl --user start buddy@ping` | `buddy/ping/status = online`, pings resume |
| Crash + auto-restart | `ssh buddy pkill -9 buddy-ping` | `offline` (last will), ~2 s later `online` again |
| Reboot | `ssh buddy sudo reboot`, wait ~1 min, then `deploy/logs.sh pong` | Both services running again without logging in |

**Done when:** a dummy service publishes and another one receives the message. ✅

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| CMake: `PahoMqttCpp` not found | Package missing | `ssh buddy sudo apt install libpaho-mqttpp-dev` (or rerun `deploy.sh --setup`) |
| CMake: target `PahoMqttCpp::paho-mqttpp3` not found ⚠️ | Target name differs in this Paho version | `ssh buddy "grep -h 'add_library' /usr/lib/aarch64-linux-gnu/cmake/PahoMqttCpp/*.cmake"` and use that name in `shared/CMakeLists.txt` |
| Compile errors in `mqtt.cpp` ⚠️ | Paho API differs from what the wrapper expects | Paste the error into chat |
| Log flips between `connected` and `connection lost` every second | Two copies of the same service (e.g. CLion + systemd) kick each other off the broker | Stop one: `ssh buddy systemctl --user stop buddy@<name>` |
| `connect … failed: … Connection refused` | Mosquitto not running | `ssh buddy sudo systemctl status mosquitto` |
| `Failed to connect to bus` from `systemctl --user` | No user session / lingering off | `ssh buddy sudo loginctl enable-linger dev` |
| `deploy/logs.sh` shows nothing or "insufficient permissions" ⚠️ | User can't read the system journal | `ssh buddy sudo usermod -aG systemd-journal dev`, then log in again |
| `start-limit-hit` in `systemctl --user status buddy@x` | Crashed 5× in 60 s | Fix the crash, then `systemctl --user reset-failed buddy@x` |
| CLion sync very slow | No local `rsync` (common on Windows): CLion falls back to SFTP ⚠️ | Acceptable, or install rsync via WSL/Cygwin |
| Services gone after reboot | Lingering off | See "Failed to connect to bus" |

## Notes for later phases

- **New service checklist:** `services/<name>/` with `CMakeLists.txt` (target `buddy-<name>`, `install(TARGETS …)`), `add_subdirectory` in the top-level `CMakeLists.txt`, topics in `topics.hpp`, name in `SERVICES` in `deploy.sh`.
- **ping/pong** stay in the repo as a template. Once 1.6 runs, turn them off: `ssh buddy systemctl --user disable --now buddy@ping buddy@pong`.
- **Phase 2 motion** already has `motion_cmd` / `motion_state`. It will also subscribe to `ears_wake` and `voice_state` to drive eyes and head, so don't rename those.
- **Commands are dropped while disconnected** (`Mqtt::publish`). Fine for events; revisit for `spotify_cmd` / `voice_say` in 1.9/1.10.
- **Offloading ears/brain to a LAN PC (D3):** Mosquitto then needs a LAN listener with a password, e.g. `/etc/mosquitto/conf.d/buddy.conf` with `listener 1883`, `allow_anonymous false`, `password_file /etc/mosquitto/passwd`. On the PC, the service runs in Docker with `BUDDY_MQTT_HOST=buddy.local` plus user/password. Not now.
- **1.6+ audio services** run as user units, so they get the user's PipeWire session thanks to lingering. ⚠️ Verify in 1.4/1.6.
- **1.12 hardening:** waiting for the Jabra after boot, systemd watchdog (`sd_notify`), journal size limits to protect the microSD.
