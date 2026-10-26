# 2.3 – Motion service

> Items marked ⚠️ are untested assumptions: check them while you build and tell Claude what you found.

## Goal

`[motion]`, a C++ service on the Pi, turns MQTT commands into the Pico's serial protocol (2.2). The rest of Buddy speaks
in joints and degrees (`head_pan: 30`). Only motion knows channels, microseconds and the serial port.

```
 any service ──buddy/motion/cmd──► [motion] ──/dev/buddy-pico (protocol v1)──► Pico ──► servos
                                      │
                                      └──► buddy/motion/state (retained): connected, joint positions
```

**Done when:**

- `mosquitto_pub -t buddy/motion/cmd -m '{"action":"move","joints":{"head_pan":30},"ms":500}'` turns the servo.
- Unplugging the Pico sets `connected:false`; replugging it recovers without a restart.
- Stopping the service makes the servos go limp within 2 s.
- `ctest` passes, including the new motion tests.

## Prerequisites

- 2.2 done: firmware flashed, `/dev/buddy-pico` exists, the picocom test passes.
- The service owns the port: close picocom before starting it.

## Design

### Joints, not channels

A joint table in code (`services/motion/joints.cpp`) is the only place that maps names to hardware:

| Field | head_pan | head_tilt | Meaning |
|---|---|---|---|
| `channel` | 0 (GP2) | 1 (GP3) | Pico channel (2.2 pinout) |
| `min_us` / `max_us` | 1000 / 2000 ⚠️ | 1100 / 1900 ⚠️ | Sent as `LIMIT` on every connect; the Pico enforces them |
| `center_us` | 1500 ⚠️ | 1500 ⚠️ | 0° |
| `us_per_deg` | 10 ⚠️ | 10 ⚠️ | MG90S ≈ 2000 µs over 180° ⚠️ |
| `max_deg_per_s` | 120 | 90 | Speed cap: the service stretches `ms` if a move would be faster |

The ⚠️ values get measured in 2.5 once the servos sit in the head. The table is code, not `buddy.env`: it describes this
robot's mechanics, not a host, and changes with the CAD, so it belongs in Git next to it. Degrees outside the joint's
range are clamped by the service; µs outside the limits are clamped again by the Pico.

### Topics (`shared/topics.json`; drop `"planned"`)

```json
{"name": "motion_cmd",   "topic": "buddy/motion/cmd",   "kind": "command",
 "payload": ["action: move|center|relax", "[joints: {name: deg}]", "[ms]"]},
{"name": "motion_state", "topic": "buddy/motion/state", "kind": "event", "retained": true,
 "payload": ["connected", "joints: {name: deg|null}"]}
```

| Command | Effect |
|---|---|
| `{"action":"move","joints":{"head_pan":30,"head_tilt":-10},"ms":400}` | All listed joints move together and arrive at the same time (`ms` stretched by the slowest joint's speed cap). `ms` default 300 |
| `{"action":"center"}` / `{"action":"center","joints":{"head_pan":0}}` | All / the listed joints to 0° (the values in `joints` are ignored) |
| `{"action":"relax"}` | All servos limp |

A multi-joint `move` is already what a Phase 3 pose or gesture step is. No new action is needed there, only callers that
send sequences.

`motion_state` is published on change only: (dis)connect, an accepted move (targets), relax. A relaxed or unknown joint is
`null`. After a reconnect every joint is `null` until the next command: the service doesn't know where an unpowered servo
was left, and doesn't move anything unasked. Unknown joint names, bad JSON or a command while disconnected are logged
once and dropped. No error topic: nobody would consume it yet.

### Inside the service

Pattern from `skeleton/CLAUDE.md`. New files in `services/motion/`:

| File | What |
|---|---|
| `main.cpp` | Flags, config, `Mqtt("motion", …)`, main loop: `notify_watchdog()`, `BlockingQueue::pop(200 ms)` for MQTT commands, PING every 500 ms, state publishing |
| `serial_link.hpp/.cpp` | termios (raw, 115200 nominal: CDC ignores the baud rate), a reader thread that splits lines and matches `<id>` replies to waiting requests (timeout 500 ms), unsolicited `*` lines into a queue. Open → `HELLO` (protocol version must be 1, else log and retry slowly) → `LIMIT` per joint → connected |
| `joints.hpp/.cpp` | Table above; `deg → µs` with clamping; the speed cap |
| `motion_protocol.hpp` | Shared with the firmware (2.2, `shared/include/buddy/`) |

**Pico missing ≠ crash.** Without `/dev/buddy-pico` the service stays up, `status` stays `online`, `state.connected` is
`false`, and it retries the port with backoff 1 → 10 s. A crash loop would restart and fail every 30 s and fill the
journal, and the rest of Buddy runs fine without motion. It still exits 1 for config errors (D14).

**Link loss** (read/write error, reply timeout ×3, `* BOOT` from an unexpected reset): close, `connected:false`, reconnect
via the handshake. The Pico relaxes by itself 2 s after the last PING, so a hung service can't leave a servo pushing.

**Shutdown:** `RELAX *`, publish `connected:false`, `mqtt.stop()`.

### Config (`deploy/config/buddy.env.example`)

```bash
# motion (2.3): the Pico (udev symlink from setup.sh pico)
#BUDDY_MOTION_DEVICE=/dev/buddy-pico
# 1 = log every serial line in both directions (debugging only: noisy)
#BUDDY_MOTION_DEBUG=0
```

### Offline entry points (no MQTT, print instead of publish)

```bash
buddy-motion --cmd move head_pan=30 head_tilt=-10 --ms 400
buddy-motion --cmd center
buddy-motion --cmd relax
buddy-motion --raw "1 MOVE 0 1500 300"     # one protocol line, prints the reply
```

## Steps

Claude writes the code; you run the checks.

1. **Code:** `services/motion/` as above, plus `add_subdirectory(services/motion)` in `CMakeLists.txt` and
   `install(TARGETS buddy-motion … COMPONENT motion)` (no new libraries: termios is libc).
2. **Topics:** `motion_cmd`/`motion_state` in `topics.json` as above (the `topics` ctest checks the generated header).
3. **Deploy wiring:** `motion` in `SERVICES` in `deploy/deploy.sh`, env keys in `buddy.env.example` (`setup.sh check`
   knows them through the example).
4. **Tests** (host, no hardware; run on the VM or the Pi):

   | ctest | Checks |
   |---|---|
   | `motion_protocol` | from 2.2: format/parse round trips, malformed lines |
   | `motion_joints` | deg → µs, clamping at both ends, speed cap stretches `ms`, multi-joint moves arrive together |
   | `motion_link` | `serial_link` against a fake Pico on a pseudo-terminal (`openpty`): handshake sends every `LIMIT`, reply matching, timeout → disconnect, `* BOOT` → re-handshake |

5. **Deploy:** stop anything holding the port, then `deploy/deploy.sh motion`. Expected: `buddy@motion active`.
6. **Run the checks below.**

## Verify

Watch everything in one terminal:

```bash
ssh buddy "mosquitto_sub -v -t 'buddy/motion/#'"
```

Expected right after the deploy: `buddy/motion/status online` and `buddy/motion/state {"connected":true,"joints":{"head_pan":null,"head_tilt":null}}`.

| # | Do | Expected |
|---|---|---|
| 1 | `ssh buddy "mosquitto_pub -t buddy/motion/cmd -m '{\"action\":\"move\",\"joints\":{\"head_pan\":30},\"ms\":500}'"` | Servo 0 turns in ~0.5 s, state shows `head_pan: 30` |
| 2 | Same with `"head_pan":200` | Stops at the joint's end, state shows the clamped angle |
| 3 | `{"action":"move","joints":{"head_pan":-30,"head_tilt":10},"ms":50}` | Both move, slower than 50 ms (speed cap), arriving together |
| 4 | Unplug the Pico's USB | `connected:false` within ~1 s; `deploy/status.sh` still shows `buddy@motion active` |
| 5 | Plug it back in | `connected:true` within ~10 s, joints `null`; a move works again |
| 6 | `ssh buddy systemctl --user stop buddy@motion` | Servos limp (immediate `RELAX *`); status `offline` |
| 7 | `ssh buddy systemctl --user kill -s KILL buddy@motion` | Servos limp after ≤ 2 s (Pico link watchdog); systemd restarts the service |
| 8 | `deploy/logs.sh motion` after a few minutes idle | Nothing new in steady state (PINGs are not logged) |

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `connected:false`, log says `/dev/buddy-pico: Device or resource busy` | picocom or a CLion run still holds the port |
| `connected:false`, `No such file` | Pico unplugged or udev rule missing: 2.2 step 8 |
| Handshake fails with `protocol version` | Firmware older than the service: `deploy/deploy.sh --firmware` |
| Servos limp after ~2 s even while the service runs | PINGs aren't arriving: main loop blocked (check the watchdog log) or `BUDDY_MOTION_DEBUG=1` to see the traffic |
| A joint moves the wrong way | Swap the sign in its `us_per_deg` (calibration, 2.5) |
| Killed every 60 s by the systemd watchdog | A blocking serial call in the main loop: serial I/O belongs on the reader thread |

## Notes for later phases

- **2.4 Eyes:** if the Pico drives the display, the protocol gets a `EYES <state>` command and motion gets
  `{"action":"eyes",…}` or a separate `buddy/eyes/cmd` topic. Decide in 2.4; protocol v1 stays compatible either way.
- **2.5 "turn on wake word":** motion only executes commands; it must not subscribe to `buddy/ears/wake` (one service,
  one job). Who decides *that* the head turns (brain, or a small behaviour service) is the 2.5 design question.
- **Phase 3:** hand joints are new rows in the joint table; gestures are timed sequences of multi-joint moves sent by
  their owner. Fine-grained sequences may want a `seq` action on the Pico later, to stay smooth over MQTT latency.
- **Phase 4:** face tracking sends small, frequent moves (10–20 Hz). The `ms`-ramp protocol handles that, but measure the
  MQTT → servo latency before tuning.
