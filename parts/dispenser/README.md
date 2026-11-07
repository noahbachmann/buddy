# Card dispenser: roller (belt conveyor)

The box (`dispenser.stl`, Dev's Onshape model) holds Buddy's hand in two piles: **H** on the left, face down over a
camera, and **B** on the lift on the right (Phase 5 plan: hand trays H and B, `feed` / `take` / `stack`). This module adds
the roller under H. It moves the **bottom card** of H either way:

- **take** (−X): out through the open left side onto the belts outside the box, where the arm picks it up.
- **feed** (+X): through the divider's slot onto the lift pile B.

`dispenser.stl` stays untouched. `dispenser-roller.stl` is the copy with the roller in place and the 4023 turned. It
also holds bought parts as stand-ins (rods, O-rings). `make_copy.py` regenerates it.

## How it works

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

## Files

All dimensions are in `params.scad`, in `dispenser.stl`'s own coordinates. Run `parts/cad build dispenser` for the
STLs and `parts/cad render dispenser` for the sheets (`parts/CLAUDE.md`).

| File | What | Print |
|---|---|---|
| `params.scad` | Box measurements, card, belts, drums, gears, layout | |
| `lib.scad` | Geometry of every piece, placed in the box frame | |
| `assembly.scad` | Preview in the box (box, lift, turned 4023, card); `part=` exports one placed piece | – |
| `roller-frame.scad` | Back plate (holds the 4023), bed, 3 ribs | bed down (upside down) |
| `front-plate.scad` | Front plate: rod ends, keeper screws, access holes for the motor screws | outer face down |
| `drive-drum.scad` | 16T gear + 3 grooves, 2 bearing seats | standing, gear down |
| `idler-drum.scad` | 3 grooves, 2 bearing seats | standing |
| `motor-gear.scad` | 20T, D-bore for the 4023's shaft, M3 set screw + nut slot | gear down |
| `gate.scad` | Gate (print **2**) | on its side |
| `make_copy.py` | `ref/` (box, lift, 4023 from `dispenser.stl`) + `dispenser-roller.stl` | – |

```bash
cd parts/tools && uv run python ../dispenser/make_copy.py    # after any change, also regenerates ref/
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

Screws go into 2.5 mm pilot holes (self-tapping into PETG).

## Print (Bambu X2D)

- PETG, 4 walls, 15 % infill. Drums, motor gear and gates at 0.12–0.16 mm layers (fits, teeth, the gap step);
  frame and front plate at 0.2.
- **No supports.** Horizontal holes are teardrops, the drum grooves are teardrop-shaped and the lower bearing seat ends in a 45° cone.
- The gate prints on its side so its gap step lies in the XY plane, where the printer is most accurate.

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

## Tuning the gap

1. Measure `card_t` (20 cards ÷ 20) and set it in `params.scad`. Everything follows from it: belt height, frame, gates.
2. If 2 cards pass or the bottom card jams, reprint only the gates, e.g. `parts/cad build dispenser gate -D gate_gap=0.4`,
   and swap them. That takes seconds, no screws.

## Dev: needed in Onshape

- **4 holes Ø3.4 in the box floor** at (x, y) = (20, −32), (60, −32), (20, −149), (60, −149).
- Keep free for the camera: the bed has a window at x 10…46, y −74…−56 (between the back edge of the card and the first belt).
- `dispenser.stl` holds a second NEMA 17 + lead screw + nut at x 354…396, outside the box. The copy keeps them as they
  are; delete them if they're a leftover.

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
