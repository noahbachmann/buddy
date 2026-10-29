# Buddy – desk robot

A stationary desk robot on a Raspberry Pi 5. It started as a local voice assistant ("Hey Jarvis, play Spotify", "…
lights off in the bedroom"), and is gaining an expressive head (Phase 2), a robotic hand (3), and an arm with a
camera (4). Every part is a small C++ or Python service; they talk only over MQTT.

```
Jabra mic → [ears] wake word, speech-to-text → [brain] intent → [spotify] [hue] [motion] … → [voice] reply
                                     phone/PC browser ⇄ [web] panel ⇄ MQTT
```

**Status and what's left:** `CLAUDE.md` → Progress and Open work.

## Where things are

| You want | Read |
|---|---|
| Status, open work, decisions, architecture | `CLAUDE.md` |
| Rebuild the Pi, config, every Phase 1 feature, troubleshooting | `docs/guides/01-setup-summary.md` |
| Your open Phase 1 checks (recordings, Hue, live tests) | `docs/guides/01-cleanup.md` |
| Current step guides (motion controller, motion service) | `docs/guides/02-*.md` |
| Train speech-to-text on your voice | `docs/voice-training.md` |
| Parts in use | `buddy-hardware.md` |
| Code map: services, shared helpers, tests, gotchas | `skeleton/CLAUDE.md` (web panel: `skeleton/services/web/CLAUDE.md`) |

## Everyday commands

From the repo's `skeleton/` folder on the PC (or the dev VM). `buddy` is the SSH alias for the Pi.

| Do | Command |
|---|---|
| Run all tests (C++ under ASan/UBSan, pytest, shellcheck) | `deploy/test.sh` (one suite: `deploy/test.sh -R Player`) |
| C++ tests on the Pi (what really counts) | `deploy/test.sh --pi` |
| Deploy everything / some services | `deploy/deploy.sh` / `deploy/deploy.sh hue web` |
| Build and flash the Pico firmware | `deploy/deploy.sh --firmware` |
| Health: services, restarts, memory, temperature | `deploy/status.sh` (`-l`: plus recent log lines) |
| Follow one service's log | `deploy/logs.sh spotify` |
| Watch the MQTT bus | `ssh buddy "mosquitto_sub -v -t 'buddy/#'"` |
| Send a command by hand | `ssh buddy "mosquitto_pub -t buddy/hue/cmd -m '{\"action\":\"off\",\"device\":\"bedroom\"}'"` |
| Restart a service on the Pi | `ssh buddy systemctl --user restart buddy@hue` |
| Web panel | `http://buddy.local:8080` (Android: `http://192.168.1.72:8080`) |
| Change config | `ssh buddy micro ~/buddy/config/buddy.env`, then restart the service. All keys: `deploy/config/buddy.env.example` |
| Record / train speech-to-text | `deploy/record.sh <label> [test]`, `deploy/train.sh` (`docs/voice-training.md`) |
| Shut the Pi down (never just pull power) | `ssh buddy sudo poweroff` |

### A service on its own: `deploy/run.sh <service> …`

Runs `~/buddy/bin/buddy-<service>` on the Pi with `buddy.env` loaded. These modes skip MQTT and print instead, but
the hardware is real: lights switch, music plays, servos move.

| Service | Examples |
|---|---|
| brain | `--test "play some jazz" "lights off"`: what Buddy would do with a sentence |
| spotify | `--cmd devices`, `--cmd play "daft punk"`, `--cmd pause`, `--cmd duck` / `restore`, `--login` (new Spotify login) |
| hue | `--cmd list` (rooms, zones, scenes, lights), `--cmd off "" bedroom`, `--cmd scene relax "living room"`, `--pair` |
| voice | `--say "Hello"` (plays on the Jabra), `--say "Hello" --wav /tmp/out.wav` |
| ears | `--transcribe a.wav b.wav`, `--wav a.wav` (whole pipeline on a file on the Pi) |
| motion | `--cmd move demo=30 --ms 400`, `--cmd gesture shake`, `--cmd relax`, `--raw "1 PING"`. Stop `buddy@motion` first: it owns the Pico's port |

## Rules that protect the hardware

1. Never power servos from the Pi or the Pico: separate 5 V supply, shared ground.
2. `sudo poweroff` before pulling the Pi's power (microSD corruption).
3. Recordings of your voice never go into Git (the repo is public); neither do `buddy.env` or the token files.
