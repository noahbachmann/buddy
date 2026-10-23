# 1.12 – Run on boot & hardening

> Written 2026-10-23. Steps 1–3 were run on the Pi by Claude (deploy, crash, freeze and PipeWire tests). Step 4
> (power cycle) needs Dev at the desk. Items marked ⚠️ are assumptions.

## Goal

Buddy starts by itself after power-on and recovers from crashes, hangs and audio hiccups without anyone logging in.
Done when: after a power cycle, "Hey Jarvis, play Spotify" works untouched.

## Prerequisites

- 1.2–1.10 done, all services deployed (`deploy/deploy.sh`).
- PC with `ssh buddy` (repo root for commands: `skeleton/`).

## What changed (D14)

| Failure | Before | Now |
|---|---|---|
| Service crashes | Restart after 2 s; after 5 crashes in 60 s systemd gave up for good | Restart after 1 s, backing off 2 → 4 → 8 → 15 → 30 s, never gives up |
| Service hangs (ALSA stall, whisper hang, deadlock) | Not noticed | Watchdog: no "alive" ping for 60 s → killed (SIGABRT) and restarted |
| Mosquitto not up yet at boot | Retry every 2 s, one log line each | Retry 0.25 → 5 s, log every 30 s; `systemctl status` shows "waiting for the broker" |
| Exception in an MQTT handler | Whole service terminated | Logged, service keeps running |
| `deploy.sh` status | `is-active` 1 s after start (could be a crash loop) | `systemctl start` waits for READY (models loaded); deploy fails if a service isn't active |
| Crash core files | Up to ~250 MB written into `~/buddy` (microSD) | Off (`LimitCORE=0`) |
| Soloist at boot | Logged in with the clock hours behind (NTP syncs ~35 s after boot) | Waits for the clock sync (max 2 min) ⚠️ suspected cause of "device Buddy not found" hours later |

How it works:
- `buddy@.service`: `Type=notify`, `WatchdogSec=60`, `Restart=always`, `RestartSteps=5`, `RestartMaxDelaySec=30`,
  `StartLimitIntervalSec=0`, `UMask=0077`, `MemoryMax=1G`, `LimitCORE=0`.
- `shared/systemd.hpp`: READY/WATCHDOG/STATUS datagrams to `$NOTIFY_SOCKET` (no libsystemd). Outside systemd (CLion,
  `--wav`, `--test`) it does nothing.
- READY is sent in `Mqtt::start()`, i.e. after models and config loaded. Watchdog pings come from each service's main loop;
  ears pings per processed audio chunk and while it waits for the device (a missing Jabra is "waiting", not "hung").

## Steps

### 1. Deploy and check

```bash
deploy/deploy.sh
deploy/status.sh
```

Expected: every `buddy@…` `active`, STATUS `connected`, RESTARTS `0`, all `buddy/<service>/status online`, throttled `0x0`.

### 2. Crash and hang tests (optional, already done 2026-10-23)

```bash
ssh buddy 'pkill -9 buddy-brain'                 # back within ~1 s
ssh buddy 'kill -STOP $(pgrep buddy-ears)'       # frozen: replaced after ~60 s ("Watchdog timeout" in the log)
deploy/logs.sh ears
ssh buddy 'systemctl --user restart buddy@brain buddy@ears'   # resets the restart counters (see Notes)
```

### 3. Audio restart test (optional, already done)

```bash
ssh buddy 'systemctl --user restart pipewire pipewire-pulse wireplumber'
deploy/logs.sh ears     # "audio read failed … reopening", then "capturing from …" within ~2 s, no restart
```

### 4. Power cycle (Dev)

1. `ssh buddy sudo poweroff`, wait until the green LED stops, unplug for 10 s, plug in.
2. Don't log in. Wait ~1 min (services start at ~8 s, Mosquitto at ~14 s, NTP sync at ~35 s).
3. Say "Hey Jarvis, play Spotify". Music should start.
4. `deploy/status.sh -l`: no restarts, nothing unusual in the last log lines.

Expected one-time log lines after boot (harmless): `connect … failed … retrying` (Mosquitto not up yet), and ~35 s
after boot `connection lost (), reconnecting` for all services: the NTP clock jump makes Mosquitto drop every client;
they're back within ~3 s.

## Verify

- [x] `deploy/status.sh` clean after deploy.
- [x] `kill -9` → back in ~1 s; 7 crashes in a row → backoff up to 30 s, never `failed`.
- [x] `kill -STOP` ears → restarted by the watchdog after 62 s.
- [x] PipeWire restart → ears reopens the device by itself.
- [ ] Power cycle: "Hey Jarvis, play Spotify" works untouched. **Done when** this is ticked.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `deploy.sh` ends with a service not `active` | It fails at start (bad config, missing model) | `deploy/logs.sh <service>`; fix; deploy again |
| RESTARTS keeps growing | Crash loop | `deploy/status.sh -l`; after fixing, `systemctl --user restart buddy@<name>` resets the counter |
| `Failed with result 'watchdog'` in the log | Service hung ≥ 60 s | Look at the lines before it; ears: audio device stalled? whisper on a huge model? |
| Service restarts every ~60 s with nothing in its log | Watchdog not fed: new loop without `systemd::notify_watchdog()` | Add the ping to the service's main loop |
| Running a service in CLion behaves differently | No `NOTIFY_SOCKET` there: READY/watchdog are no-ops (intended) | — |
| `Spotify isn't running on Buddy` after boot or hours later | Soloist lost its session ⚠️ | `systemctl --user restart buddy-soloist`; note when it happened and tell Claude |
| Everything restarts ~35 s after boot | NTP clock jump, see step 4 | Harmless |

## Notes for later phases

- **Restart counter:** systemd resets it only on a manual start/stop/restart, so after several crashes over weeks the next
  restart waits the full 30 s. `systemctl --user restart buddy@<name>` resets it.
- **New services** get readiness for free (Mqtt::start) but must call `systemd::notify_watchdog()` in their main loop.
  motion (2.3): ping only when the Pico answers, so a stuck serial link restarts the service.
- **journald** stays RAM-only (Raspberry Pi OS default, `Storage=volatile`): logs are gone after a reboot. For post-mortems
  of boot problems, `Storage=persistent` with `SystemMaxUse=50M` would keep them at a small microSD cost. Not done.
- **No sudo without password** for Claude on the Pi: system-level changes go into `setup-pi.sh` and run with `deploy.sh --setup`.
