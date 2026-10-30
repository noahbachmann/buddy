# CAD prompt: Buddy's neck, head and body

> For Dev: fill every `{{…}}`, delete what doesn't apply, add what's missing, then paste everything below the line into
> a Claude Code session in this repo. Measure with the calliper, with cables plugged in. Stage 1 belongs to 2.5,
> stage 2 to 2.6; you can run them as two separate sessions.

---

You're designing the printed parts of **Buddy**, a stationary desk robot (repo `dev/robots/buddy`). Read first:
`CLAUDE.md`, `buddy-hardware.md`, `docs/plans/02-05-head-pan-tilt.md`, `docs/plans/02-06-head-body.md`, and
`parts/arm/` (README + `params.scad`, `lib.scad`) as the style to follow.

## What to deliver

**Stage {{1 or 2}}** (do only this one):

1. **Stage 1 (step 2.5): neck module + test stand + dummy head** in `parts/neck/`.
   - Neck module: pan on a {{6808-2RS / other}} bearing with a torque-only coupling to the pan servo's horn, tilt fork
     with the servo horn on one cheek and a 623-2RS idler on the other, cable slot through the pan axis, mechanical end
     stops just past the software limits, a servo-connector holder at its base.
   - Interfaces: bottom bolt pattern (the body will use the same one), top head mount plate.
   - Test stand with the same bolt pattern and a printed protractor for pan and tilt.
   - Dummy head: plate on the tilt fork with a ballast pocket; total head mass {{300}} g at the planned CoM.
2. **Stage 2 (step 2.6): inner chassis, body shells, head shell** in `parts/body/` and `parts/head/`.
   - Chassis first (everything functional, printable and testable open), shells after.

Before you model anything: list your assumptions and the open questions, and wait for my answers.

## Format

- Parametric **OpenSCAD**, laid out like `parts/arm`: every dimension in `params.scad`, shared geometry in `lib.scad`,
  `assembly.scad` posed (pan/tilt angles as variables) with stand-ins for every bought part, `build.sh` renders STLs
  already in print orientation into `stl/`.
- Check collisions in `assembly.scad` across the full pan and tilt range, including the cable slot and loop space.
- A `README.md` per folder: parts list (bought parts with sizes), print table (file, orientation, material, supports),
  assembly steps with *why*, ⚠️ list of untested assumptions. I'm new to mechanical design and printing: explain the
  steps, not just list them.

## Hard constraints

- **Printer:** Bambu Lab X2D + AMS, two nozzles. 256 × 256 × 260 mm with one nozzle, 235.5 × 256 × 256 mm with both.
  Split bigger parts with keyed or doweled joints.
- **Tolerances from my 2.1 test print:** hole oversize {{… mm}}, press fit for bearings {{… mm}}, sliding fit
  {{… mm}}, M3 heat-set insert hole {{Ø … × … mm}}.
- **No supports** wherever the geometry allows (45° roofs, teardrop horizontal holes, bridges ≤ {{20}} mm). Where a
  support is unavoidable: PETG part, PLA support interface from the second nozzle; say which parts need it.
- **Fasteners:** M3 socket screws into heat-set inserts for everything that comes apart; M2.5 for the Pi.
- **Golden rule:** servo power never through the Pi. The servo supply's barrel socket and power switch mount in the
  back panel; the Pi gets its own USB-C cable.

## Materials

Only **PETG** and **PLA**, no TPU.

| Parts | Material |
|---|---|
| Structure, anything loaded, with a fit, or near the Pi: neck module, chassis, sled, brackets, body shell around the Pi | PETG |
| Support interface under PETG | PLA (second nozzle) |
| Head shell, face plate, accents | {{PETG / PLA}}, colours {{…}} |
| Test stand, dummy head, jigs | PLA |
| Damping (Jabra cradle lining, neck mount, feet, grommets) | Bought rubber/foam parts: design pockets and holes for {{rubber grommets …, feet Ø …, pad thickness …}} |

Print settings: 0.2 mm layers, 4 walls, 15 % gyroid unless a part needs more (say which and why).

## Parts to house (my measurements)

| Part | Size (mm) | Notes |
|---|---|---|
| Raspberry Pi 5 + Active Cooler | 85 × 56, height {{…}} with the USB stack | Holes M2.5 on 58 × 49. Measure plugged-in cable bend room: USB-C {{…}}, USB-A (Jabra) {{…}}, USB-A (Pico) {{…}}. microSD to a service hatch |
| Jabra Speak 510 | Ø 120 × 33, 195 g | Position: {{A flat on the top deck / B upright, grille toward me}}. Fixed USB cable, length {{…}}. Grille free, foam-lined cradle, buttons {{reachable / covered}} |
| Pico board (breadboard now, perfboard later) | {{82 × 55 × …}} incl. wires | Removable sled; BOOTSEL reachable; room for 5 more servo cables later |
| Servo DC socket + switch | socket {{…}}, switch {{…}} | Back panel |
| MG90S (×2) | {{22.8 × 12.2 × 28.5}}, flange {{32.5}} wide, spline {{…T}}, horn {{…}} | Measure the horn you use |
| Pan bearing | {{40 × 52 × 7}} | |
| Tilt idler bearing | 3 × 10 × 4 (623-2RS) | M3 bolt |
| Servo extension through the neck | Plug {{…}} | |

## Mechanics (from the 2.5 plan)

- Pan ±80°, tilt −20…+30°; end stops just past these.
- Tilt axis through the head's centre of mass (ballast pocket to trim it). The head must stay put with the servos limp.
- Head mass budget ≤ {{300}} g including future eyes and camera.
- Cable slot through the pan axis sized for: tilt servo cable, eyes (≈ 9 wires), camera FPC (≥ 20 × 3 mm), with a
  service loop for ±80° and no sharp edges.
- No bought part may be loaded in a way it isn't made for (no axial head load on a servo shaft).

## Body (stage 2)

- Inner chassis carries everything; shells only cover it and can be redesigned without touching the chassis.
- Footprint at most {{… × …}} mm, total height about {{…}} mm. Must not tip when the head moves fast.
- **Air:** the Active Cooler is a blower: intake above it, a vent at its side exhaust, ≥ 5 mm gap around the heatsink,
  the Jabra mic away from the exhaust.
- **Acoustics:** the Jabra and the neck module are decoupled from the chassis (rubber/foam), the grille free, no
  booming cavity behind it.
  Nothing rattles.
- **Radio:** no metal near the Pi's antenna end; keep that end high.
- **Cables:** every one in a channel or clip, strain relief where it leaves the body, a connector where the neck cables
  enter the chassis. Only two cables leave Buddy: the Pi's USB-C and the 5 V servo supply.
- **Reserve:** face plate with two round eye openings (GC9A01, visible Ø {{33}} mm, centres {{…}} mm apart), a camera
  spot between them (Camera Module 3, 25 × 24 mm), a side face of the chassis that can take the arm's shoulder plate
  later (90 × 80, 4 × M4 on 70 × 56). Don't design the arm or eyes now, only keep the room.

## Look

{{Describe the style: e.g. "friendly, rounded, like a small desk lamp robot", reference pictures, colours, whether
the neck shows or is covered, size relative to the Jabra.}}

## Anything else

{{Extra wishes, things to avoid, deadlines.}}
