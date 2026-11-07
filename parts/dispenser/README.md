# Card dispenser: roller (belt conveyor) and pusher

The box (`dispenser.stl`, Dev's Onshape model) holds Buddy's hand in two piles: **H** on the left, face down over a
camera, and **B** on the lift on the right (Phase 5 plan: hand trays H and B, `feed` / `take` / `stack`). This module adds
the roller under H and the pusher on the right wall. The roller moves the **bottom card** of H either way:

- **take** (−X): out through the open left side onto the belts outside the box, where the arm picks it up.
- **feed** (+X): through the divider's slot onto the lift pile B.

The pusher moves **all of B** back: **stack(B onto H)** (−X): the lift raises B to the pusher, which pushes the pile
over the divider into H's chute, where it drops onto H.

`dispenser.stl` stays untouched. `dispenser-roller.stl` is the copy with the roller and the pusher in place, the 4023
turned and the box cut as Dev needs to cut it (below). It also holds bought parts as stand-ins (motors, rods, screw, nut,
O-rings). `make_copy.py` regenerates it.

## How the roller works

```
            left gate (x −5…0, under the left wall)      right gate (x 66.5…71.5)   divider slot
                 │  pile H (x 0…66.5)                          │                  │
   ──────────────▼────────────────────────────────────────────▼─────────────────▼──────── z 66.04
   ═══════════════════════ belts (3 O-rings), top at z 65.56 ══════════════════   lift / B
  (idler drum x −55.7)          bed: belts slide on it            (drive drum x 62) ◄─ 20:16 ◄─ 4023
```

- **Belts:** three NBR O-rings (80 × 3 mm) at y −78 / −98 / −118, running on a printed drive drum and idler drum. They are
  stretched 7 % (279.0 mm path, 260.8 mm free), which gives tension without a tensioner. The pile rests on the belts,
  rubber on card, so only the bottom card moves.
- **Gates:** a bar across the card path, `gap` = 1.5 card thicknesses (0.48 mm) above the belt top. The bottom card fits
  under it, the second one doesn't. One gate stands on each side of the pile. Both sit at the height of the divider's
  upper block (z 66.04), so the belt top is `66.04 − gap`. The divider's 1.04 mm slot is now only clearance: the right gate
  does the separating, and it can be swapped to tune it.
- **Bed:** the belts slide on a flat bed. One O-ring strand carries only ~2.5 N, so a pile of 20 cards would sag a free
  span by ~1.4 mm and the cards would sit on the bed instead of the rubber.
- **Drive:** the 4023 is turned 90° (shaft to the front, connector to the left) and sits behind the back plate. A 20T
  gear on its shaft drives the 16T gear on the drum: 1.25 drum turns per motor turn, 54.6 mm of belt per motor turn,
  0.27 mm per full step. With the gear pair the drum can sit right at the divider (1 mm gap). A drum directly on the
  motor shaft would end 12 mm further left, because the motor body is 42 mm square.
- **Direction:** motor **clockwise seen from its shaft end** (that is, from the front) = **take**, counter-clockwise =
  feed. Which way "positive steps" turn depends on the coil wiring and the driver: set it once in the firmware (DIR
  inverted or not), no hardware change needed.
- **Why the front plate is separate:** a closed O-ring can't go around anything that is held at both ends. With the
  front plate off, everything inside the loops (drums, bed, ribs) hangs from the back plate, so the O-rings slide on
  from the front.

## How the pusher works

```
  the carriage seen from +X                    on the line y −156.5, z 120, from the right:
                                               17HS4401 (outside) → coupling (x 119…144) → screw (x 19…131)
  z 143  ┌─────────┬────┐
         │upper bar│ear ╲      (bar = bushing on a Ø5 rod)
  z 132  ├─────────┤     ╲────────────────────────────┐
  z 120  │ nut web │ ← the coupling passes here        │  plate: 35 high, its face pushes the pile
  z 108  ├─────────┤                                   │
  z  97  │lower bar│───────────────────────────────────┘  ← 0.16 above the lift, 1.16 above the divider
         y −166    −147 −144                          −55 (lift riser at −54)
```

- **Where it runs:** three things fix the layout. The divider block (z 66…96) spans the full depth, so everything that
  crosses it stays above z 97. The lift's riser (x 98…126, y −54…−50) rises to z 144 while pushing, so nothing may
  sit behind the cards. The cards cover y −142…−54, and the arm drops cards onto H from above. That leaves the **front
  strip** y −166…−147: rods, screw, nut and the carriage bars run there, on one vertical line at y −156.5. Only the
  plate crosses the card area.
- **Carriage** (`pusher.scad`): the plate (35 × 89 × 5, face over y −144…−55) plus two bars that slide on two Ø5
  steel rods and a web that holds the TR8 nut. The rods carry the carriage; the nut only drives it. So the screw hangs
  free at its far end, as on a printer's Z axis: no third support that would have to line up.
- **Why the nut sits ahead of the plate:** a coupling (Ø19 × 25) joins the motor shaft and the screw, and it sits where the
  plate rests. So the nut sits left of the coupling and the plate reaches around it: the gap between the plate's two
  front tabs lets it pass the coupling.
- **Home:** the plate sits in a notch in the right wall, face flush with the wall (x 145). The mount closes the notch
  from outside and is the end stop. Home against the mount at low current: sensorless (TMC2209 StallGuard, if the
  board has it, still undecided) or with one of the 4 limit switches on the shopping list.
- **Push:** lift plate top to **z 97.04** (`push_lift` = divider top + 1). The plate's lower edge is at **z 97.20**, half a
  card above the lift: it catches the bottom card's edge and clears the lift and the divider. The plate travels
  **79 mm** (x 145 → 66, ~10 screw turns): the pile then lies at x 3…66, fully over H's chute (x 0…72), and drops. Then
  back home, lower the lift.
- **Direction:** the TR8x8 is right-handed. Which way "positive steps" turn depends on the wiring: set DIR in the firmware.
- ⚠️ The pile drops 30 mm (z 97 → H's top at ~66). It lands between the left wall and the divider, 9 mm wider than a
  card. Test with a real pile; if it lands skewed, try a slower push first.

## Files

All dimensions are in `params.scad`, in `dispenser.stl`'s own coordinates. Run `parts/cad build dispenser` for the
STLs and `parts/cad render dispenser` for the sheets (`parts/CLAUDE.md`).

| File | What | Print |
|---|---|---|
| `params.scad` | Box measurements, card, belts, drums, gears, layout | |
| `lib.scad` | Geometry of every piece, placed in the box frame | |
| `assembly.scad` | Preview in the box (cut box, lift, turned 4023, card, pile B); `part=` exports one placed piece; poses `-D push=0…1 -D lift_top=97.04` | – |
| `roller-frame.scad` | Back plate (holds the 4023), bed, 3 ribs | bed down (upside down) |
| `front-plate.scad` | Front plate: rod ends, keeper screws, access holes for the motor screws | outer face down |
| `drive-drum.scad` | 16T gear + 3 grooves, 2 bearing seats | standing, gear down |
| `idler-drum.scad` | 3 grooves, 2 bearing seats | standing |
| `motor-gear.scad` | 20T, D-bore for the 4023's shaft, M3 set screw + nut slot | gear down |
| `gate.scad` | Gate (print **2**) | on its side |
| `pusher.scad` | Pusher carriage: plate, 2 rod bars, nut web | plate down (right face) |
| `pusher-mount.scad` | Holds the 17HS4401 and the rods' right ends on the wall's outer face | inner face down |
| `make_copy.py` | `ref/` (box, lift, 4023 from `dispenser.stl`) + `dispenser-roller.stl` (cut box, roller, pusher) | – |
| `check_pusher.py` | Collisions over the pusher's stroke with the lift at push height (lift rebuilt from measured boxes) | – |

```bash
cd parts/tools && uv run python ../dispenser/make_copy.py    # after any change, also regenerates ref/
cd parts/tools && uv run python ../dispenser/check_pusher.py # after a pusher, lift or box change: must print 0 overlapping
```

## Parts

| Qty | Part | Note |
|---|---|---|
| 3 | NBR O-ring 80 × 3 mm (ID × cord), 70 Shore A | The belts. ⚠️ Different size: change `oring_id`, the drum distance follows |
| 4 | MR105ZZ bearing, 5 × 10 × 4 | Pressed into the drum ends |
| 2 | Steel rod Ø5 mm, 122 mm | Silver steel, cut and deburred. Fixed: the drums turn on their bearings |
| 3 | M3×6 socket head | 4023 to the back plate (the 4th hole would cut into the drive rod's hole) |
| 1 | M3×4 set screw + M3 nut | Motor gear onto the shaft's flat |
| 2 | M3×6 + M3 washer | Rod keepers on the front plate (the washer covers the rod end) |
| 3 | M3×12 | Front plate into the rib ends |
| 4 | M3×12 | Frame to the box floor, from below |

**Pusher**

| Qty | Part | Note |
|---|---|---|
| 1 | 17HS4401 + flexible coupling 5/8 + TR8x8 lead screw + TR8x8 brass nut | The second set on the shopping list |
| 2 | Steel rod Ø5 mm, 157 mm | Silver steel like the roller's, cut and deburred |
| 4 | M3×6 socket head | Motor to the mount, from the inside; the heads sink into the mount |
| 8 | M3×10 countersunk | Mount to the wall, from inside the box (heads flush: the lift slides past) |
| 2 | M3×8 | Nut flange to the web (only the 2 holes above and below the screw) |

⚠️ **Cut the lead screw to 111.5 mm** (hacksaw, then file a chamfer so the nut starts cleanly). Uncut, 200 mm would
stick 65 mm out of the left wall. Cut the end that goes into the coupling; the factory end
stays free, so the nut threads on easily.

Screws go into 2.5 mm pilot holes (self-tapping into PETG).

## Print (Bambu X2D)

- PETG, 4 walls, 15 % infill. Drums, motor gear and gates at 0.12–0.16 mm layers (fits, teeth, the gap step);
  frame and front plate at 0.2.
- **No supports.** Horizontal holes are teardrops, the drum grooves are teardrop-shaped and the lower bearing seat ends in a 45° cone.
- The gate prints on its side so its gap step lies in the XY plane, where the printer is most accurate.
- Pusher carriage: plate down, the bars stand, so the bushings come out round (vertical holes). The web over the nut
  is the only bridge (24 mm). 0.16 mm layers. Mount: inner face down, 0.2 mm.

## Assembly

1. **Bearings:** press 2 MR105 into each drum, pushing only on the outer race.
2. **Motor:** put the 4023 behind the back plate, boss in the recess and connector to the left. Screw it on with 3× M3×6
   from the front; the heads sink into the plate. The drum gear will run over them.
3. **Motor gear:** on the shaft, flush with the drum gear's plane (7 mm in front of the motor face). Tighten the set screw on the flat.
4. **Drums:** push the rods into the back plate (blind holes), then slide the drums on with the drive drum's gear at the back, meshing.
5. **Belts:** slide the 3 O-rings over both drums from the front, into the grooves. They pass over the bed and the ribs.
6. **Front plate:** rod ends into its holes, 3× M3×12 into the rib ends, 2 keeper screws with washers.
7. **Into the box:** the frame's step sits against the left edge of the box floor. 4× M3×12 from below.
8. **Gates:** press them into their sockets: one under the left wall, one in front of the divider.

**Pusher** (after Dev's cuts in the box):

1. **Motor:** 4× M3×6 from the mount's inner face into the 17HS4401, boss in the recess.
2. **Coupling:** on the motor shaft, its right end 6 mm from the mount's inner face (x 144). Tighten the 5 mm side.
3. **Carriage:** thread the nut onto the screw, flange towards the motor; slide the flange onto the web's right face and
   screw it on (2× M3×8). Push both rods through the bushings.
4. **Into the box:** rods' left ends into the left wall's holes, at an angle through the wall's notch. Screw end
   towards the motor.
5. **Mount on:** the rods' right ends go into the mount's sockets, the screw into the coupling. 8× M3×10 countersunk
   from inside the box. Tighten the coupling's 8 mm side through the open front.
6. **Check:** turn the screw by hand. The carriage must run its full 79 mm without binding. If it binds, loosen the
   mount screws, let it centre, and tighten again.

## Tuning the gap

1. Measure `card_t` (20 cards ÷ 20) and set it in `params.scad`. Everything follows from it: belt height, frame, gates.
2. If 2 cards pass or the bottom card jams, reprint only the gates, e.g. `parts/cad build dispenser gate -D gate_gap=0.4`,
   and swap them. That takes seconds, no screws.

## Dev: needed in Onshape

- **4 holes Ø3.4 in the box floor** at (x, y) = (20, −32), (60, −32), (20, −149), (60, −149).
- Keep free for the camera: the bed has a window at x 10…46, y −74…−56 (between the back edge of the card and the first belt).
- **Pusher notch** in the right wall: y −165…−54, z 96.2…144.2, through, open to the front edge (the plate's pocket).
- **8 holes Ø3.4 through the right wall**, 90° countersink Ø6.5 on the **inside** (the lift slides past: heads flush), at
  (y, z) = (−155, 92.2), (−100, 92.2), (−60, 92.2), (−155, 148.2), (−100, 148.2), (−60, 148.2), (−48, 104), (−48, 136).
- **2 blind holes Ø5.2, 3 mm deep, in the left wall's inner face** (the rods) at (y, z) = (−156.5, 102.7), (−156.5, 137.7).
- The spare 17HS4401 + screw + nut at x 354…396 are now the pusher's: the copy drops them and places its own.
- The motor sticks out 13 mm in front of the box (y −178), the mount 15 mm: keep that free.

## ⚠️ Open: feed stops short

A fed card is driven only while it sits on the belt. Its trailing edge stops over the drive drum (x 62), so the card
ends at x 62…125, while the divider ends at x 77. The last 15 mm still sit in the slot and under the right gate, and
the next card would jam on them. No roller left of the divider can push further. Options:

1. **Software, test first:** feed fast so the card slides on over pile B until it hits the right wall (x 145). It then
   ends at x 82…145, clear of the divider and squared against the wall. That needs ≳ 0.35 m/s at the belt (~6 drum
   turns/s, ~300 rpm at the motor; card on card μ ≈ 0.3, 20 mm to slide). ⚠️ A pancake stepper should reach that
   without load. Test whether it does with the card.
2. **Hardware:** a driven pinch roller on the lift side behind the slot, geared from the drum through the gear zone
   behind the cards (y −36…−43). It would have to get out of the way when the lift pushes B back over to the left.

## ⚠️ Untested assumptions

- `card_t` = 0.32 mm is a guess (sleeves change everything).
- The MR105 press fit (`press`), the 5.2 mm rod holes and the printed gear mesh (`gear_slack`, `gear_backlash`) need a test print.
- NBR on the card back is assumed to grip better than card on card. If the second card comes along, lower the gap first.
- The 4023's model (connector side, shaft flat at 2.0 mm from the axis) is taken from `dispenser.stl` as drawn.
- Pusher: the bottom card is caught by half its thickness (0.16 mm). That needs `card_t` measured and the lift's height
  calibrated once (raise in 0.05 mm steps until a single card on the lift gets pushed). Bowed cards may slip under the
  plate. If they do, the robust fix is a comb: slots in the lift plate (Dev's part) so the plate's lower edge can dip
  below the lift top.
- Pusher bushings (Ø5.4 printed on Ø5 steel) and the coupling and nut dimensions (Ø19 × 25, TR8 nut Ø22 flange on
  Ø16 bolt circle, as Dev drew it) are untested.
