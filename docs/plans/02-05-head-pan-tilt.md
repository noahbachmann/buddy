# 2.5 – Head pan/tilt: plan

> A plan, not a guide: decisions, parts and the order of work. `docs/guides/02-05-head.md` replaces this file once the
> decisions at the end are made and the neck module is printed. ⚠️ = untested assumption.

## Goal

Buddy's head turns left-right (pan) and nods (tilt) on the two MG90S servos from 2.2. On the wake word it looks at Dev;
when the conversation is over it goes back to an idle pose and lets the servos go limp.

2.5 builds the **neck module** (bearing, both servos, tilt fork, cable path) as a self-contained part, tested on a
printed stand with a **dummy head** of the planned mass. 2.6 only builds the head shell and body around it. So the
mechanics get proven before anything big is printed (golden rule 3).

**Done when:**

- The neck module carries the dummy head: pan ±80°, tilt −20…+30° without binding or wobble you can see.
- With both servos relaxed, the head stays where it was left (doesn't nod forward or drift).
- `head_pan` / `head_tilt` in `services/motion/joints.cpp` hold measured values (centre, limits, µs per degree); the
  `demo` joint is gone.
- "Hey Jarvis" → the head reaches its attention pose in ≤ 500 ms ⚠️ after `buddy/ears/wake`. About 10 s after the
  reply ends: idle pose, then relax.
- Head motion doesn't cost STT accuracy: a test session recorded with the head moving at every clip start scores
  within one intent of the same sentences without movement (`deploy/train.sh eval`).
- Pan and tilt moving together under the head's load: servo rail ≥ 4.8 V, `vcgencmd get_throttled` = `0x0`.

## Prerequisites

- 2.2 wired and tested (steps 1–3, 9), 2.3 Verify done (open work, Dev #1).
- **2.1 printer guide and tolerance print.** The neck module is the first part that needs real fits (bearing press
  fit, servo pocket, M3 holes, horn on spline). The guide doesn't exist yet: Claude writes it first.

## What "turns toward the user" can mean in Phase 2

Be clear about this before building: **Buddy doesn't know where you are.** The Jabra delivers one mono channel, so
there is no direction of arrival, and there is no camera until 4.2. In 2.5 "toward the user" means a fixed
**attention pose**: the pan/tilt angle where Dev usually sits, set once in config. Real "look at whoever speaks" comes
with face tracking (4.3). A mic array (e.g. ReSpeaker) would give a direction, but it replaces the Jabra, needs new
voice training and turns into a second audio project. Not worth it for this step.

## Mechanics: the neck module

### Load paths (why it is built this way)

- **Pan carries the head's weight axially.** If the servo spline carries it, the head wobbles on the servo's tiny
  bearing and the MG90S output shaft and gears wear out. That's the same reasoning as the 608 in the arm
  (`parts/arm/README.md`). A real bearing carries the head; the servo only supplies torque.
- **Torque-only coupling.** If the bearing *and* the servo shaft both locate the turntable, any misalignment between
  them bends the servo shaft (over-constrained). So the servo horn drives the turntable through a dog or slot coupling
  with a little radial play: torque passes through, side loads don't.
- **Tilt axis through the head's centre of mass.** Then the tilt servo holds almost no static torque. The head stays
  put when relaxed, and a servo that isn't pushing doesn't hum into the mic. The head gets a ballast pocket (nuts or
  steel balls) so the CoM can be trimmed onto the axis after eyes and camera go in.
- **Tilt joint = the arm's knuckle/fork pattern, scaled down:** the servo horn screwed to one cheek of a fork, the
  other cheek on a small bearing (623-2RS on an M3 bolt). Same idea, known to work, one style across the robot.

### Pan bearing options

| Option | Good | Bad |
|---|---|---|
| **A: thin-section ball bearing around the axis** (6808-2RS, 40 × 52 × 7 mm ⚠️ final size from CAD), servo below it on the axis | Real bearing, takes axial and tipping loads, no visible wobble; the 40 mm bore leaves room for the coupling *and* a cable slot | Must be bought; press fit depends on the 2.1 tolerance test |
| B: lazy-susan turntable ring | Cheap, large | Loose, rattles (steel noise into the mic), large diameter |
| C: servo carries the head directly | Simplest | Wobble, shaft side load, MG90S wears out. No |

**Recommendation: A.** The head is light (budget below), so a single 7 mm wide bearing is enough for the tipping moment.

### Budgets

| | Value | Why |
|---|---|---|
| Head mass, everything included (shell, tilt servo, 2 round displays, camera later, ballast) | ≤ 300 g ⚠️ | Keeps pan inertia low, so moves stay fast and quiet |
| Tilt torque | ≤ 0.3 kg·cm static (CoM ≤ 1 cm off the axis) | MG90S ≈ 1.8 kg·cm at 4.8 V ⚠️: holding at < 20 % of that keeps heat and hum low |
| Pan range | ±80° (servo 180°, 10° margin each end) | Margin for the cable loop and horn spline steps |
| Tilt range | −20° (down) … +30° (up) | Look at the desk, look at Dev's face |
| Mechanical end stops | Just past the software limits | Protect cables if a horn sits one spline tooth off; the software limits are primary, a servo must never stall against a stop |

### Cables through the neck

| Now / later | Cable | Through the pan axis as |
|---|---|---|
| 2.5 | Tilt servo, 3 wires | Round cable in the bore slot |
| 2.4 (later) | Eyes: 2 × GC9A01 over SPI ≈ 9 wires | Ribbon or a thin multi-core cable |
| 4.2 (later) | Camera Module 3: flat flex cable to the Pi 5's 22-pin CSI port | **Slot ≥ 20 × 3 mm** ⚠️, no sharp edges: FPC kinks easily |

- Pan is never continuous: ±80° winds and unwinds a **service loop** below the bearing (clock-spring style). Size the
  slot and the loop space for all three cables now, so 2.4 and 4.2 don't mean redesigning the neck.
- The tilt cable crosses the tilt joint close to its axis, where the length barely changes as the head nods.
- **A connector at the bottom of the neck module** (servo extension plug, later the eyes connector), so head and neck
  come off without opening the Pico board.

### Test stand and dummy head

- **Stand:** a printed foot plate that the neck module bolts onto with the same bolt pattern the 2.6 body will use,
  with a printed **protractor scale** for pan and tilt (needed for calibration).
- **Dummy head:** a plate on the tilt fork with ballast (nuts, coins) at the planned 300 g and CoM. Cardboard face for
  looks. It proves the bearing, the torque budget, the cable loop and the relaxed head staying put.

## Electronics

- Pan on ch0 (GP2) and tilt on ch1 (GP3), already wired that way in 2.2. Tilt servo lead too short for the neck: a
  30 cm servo extension.
- Two MG90S on the 5 A rail: stall ~0.7 A each ⚠️, plenty of margin. Keep the 1000 µF on the rail.
- The breadboard stays on the test stand until 2.6 brings the perfboard.

## Software

1. **Joint table** (`joints.cpp`): `head_pan` takes ch0, the `demo` row goes. Calibrate on the stand: centre each servo
   at 1500 µs *before* fitting the horn (as in the arm's assembly step 4), then step the pulse to each end, note the µs
   at the mechanical limit minus margin, and get µs per degree from two marks on the protractor. Speed caps
   (`max_deg_per_s`) are tuned during the noise measurement below.
2. **Gestures** (`gestures.cpp`): the `demo` gesture goes. Proposal: real head gestures `nod` (yes) and `shake` (no).
   The Andi joke could stay as a head shake (`shake`: same brain rule and `commands.tsv` line), or go entirely.
3. **Who decides that the head moves** (open since 2.3): a new C++ service **`behaviour`**, "Buddy's body language".
   It subscribes to `buddy/ears/state` and `buddy/voice/state` (later `buddy/spotify/state`) and publishes
   `buddy/motion/cmd`. motion stays a pure executor, brain stays text → intent. Later it also owns the eyes' states
   (2.4) and idle animations, which is why it isn't called `head`. No new topics needed beyond its `status`.

   | Input | Behaviour |
   |---|---|
   | `ears/state: listening` (wake word) | Move to the attention pose (`BUDDY_BEHAVIOUR_ATTENTION=pan,tilt`, where Dev sits) |
   | `ears/state: transcribing` | Small "thinking" tilt |
   | `voice/state: speaking` | Hold the attention pose |
   | Everything idle for `BUDDY_BEHAVIOUR_IDLE_S` (10 s) | Idle pose, then `relax` |
   | `motion/state: connected false` | Nothing: when the Pico comes back every joint is `null`, the next event moves it |

   Why config and not code for the attention pose: it describes where Buddy stands on *this* desk relative to Dev, not
   the mechanics (those stay in `joints.cpp`).
4. **Panel:** motion stays off it (D19, no login). Only a `behaviour` chip in `SERVICE_CHIPS`.
5. **Tests:** the behaviour state machine as a `behaviour_core` lib, without I/O (events in, commands out, fake clock);
   updated joint and gesture tests in motion.

## Measure (how to work, rule 5)

| What | How | Target |
|---|---|---|
| Wake → pose reached | Timestamps of `buddy/ears/wake` and `buddy/motion/state` from `mosquitto_sub -v` | ≤ 500 ms ⚠️ |
| STT with the head moving | `record.sh` gets an option that plays the attention move at each clip start; record the `test` set, `train.sh eval` against the existing test session | ≤ 1 intent worse, WER within 1 % |
| Holding hum | Clip with the head held vs relaxed | Relax rule confirmed, or the idle timeout shortened |
| Rail under load | Multimeter on the rail during pan + tilt | ≥ 4.8 V, `get_throttled` `0x0` |

The servo noise lands right where ears starts listening. If the STT numbers drop, the levers in order: slower and
shorter attention move, the move finishing before the speech starts (ears' VAD start), TPU damping (2.6), quieter servos.

## Parts to buy

| Qty | Part | For | Note |
|---|---|---|---|
| 1 | 6808-2RS bearing (40 × 52 × 7) ⚠️ | Pan | Final size from the CAD; buy together with the 623s |
| 2 | 623-2RS bearing (3 × 10 × 4) | Tilt idler (+1 spare) | |
| 1 | Servo extension cable 30 cm | Tilt servo through the neck | |
| – | M3 heat-set inserts (M3 × 5.7 short) + M3 socket screws assortment (6–16 mm) | All screwed joints | Inserts survive repeated assembly; self-tapping into PETG doesn't |
| 1 | Temperature-controlled soldering iron (e.g. Pinecil) + heat-set insert tip | Inserts now, perfboard in 2.6 | Needed for 2.6 anyway, so buy it now |

Suppliers: Bastelgarage / Digitec Galaxus first; bearings maybe from a CH bearing shop. Prices go into the guide.

## Order of work

| # | Who | What |
|---|---|---|
| 1 | Dev | 2.2 / 2.3 live (open work, Dev #1) |
| 2 | Claude → Dev | 2.1 guide; print the tolerance test, report the fits |
| 3 | Dev | Buy the parts above; measure MG90S, horn, bearings (calliper) for the CAD prompt |
| 4 | Claude | Neck module + stand + dummy head CAD (`docs/plans/02-06-cad-prompt.md`, stage 1) |
| 5 | Dev | Print, assemble, centre servos, calibrate |
| 6 | Claude | Joint table, gestures, `behaviour` service + tests (can start in parallel with 4–5) |
| 7 | Dev + Claude | Measure (table above), tune speeds and timeouts |
| 8 | Claude | Guide `docs/guides/02-05-head.md` replaces this plan; progress table, `buddy-hardware.md` |

## Decisions for Dev

| # | Question | Recommendation |
|---|---|---|
| 1 | Pan bearing | A: thin-section bearing + torque-only coupling |
| 2 | Body-language service | New `behaviour` service (not brain, not motion) |
| 3 | Andi demo | Keep it as a head `shake`, or delete it with the `demo` joint |
| 4 | Attention pose | Fixed, from `buddy.env`, until 4.3 |

Once decided, they become a decision row (D21 "Neck & body language") in `CLAUDE.md`.
