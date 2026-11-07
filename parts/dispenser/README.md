# Card dispenser: roller (belt conveyor) and pusher

The box (`dispenser.stl`, Dev's Onshape model) holds Buddy's hand in two piles: **H** on the left, face down over a
camera, and **B** on the lift on the right (Phase 5 plan: hand trays H and B, `feed` / `take` / `stack`). This module adds
the roller under H and the pusher on the right wall. The roller moves the **bottom card** of H either way:

- **take** (−X): out through the open left side onto the belts outside the box, where the arm picks it up.
- **feed** (+X): through the divider's slot onto the lift pile B.

The pusher moves **all of B** back: **stack(B onto H)** (−X): the lift raises B to the pusher, which pushes the pile
over the divider into H's chute, where it drops onto H.

`dispenser.stl` stays untouched. `dispenser-roller.stl` is the copy with the roller and the pusher in place, the 4023
turned and the box changed as Dev needs to change it (below: cradle, notch, holes). It also holds bought parts as
stand-ins (motors, rods, screw, nut, O-rings). `make_copy.py` regenerates it.

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
  seen from the front (−Y), carriage at home                       seen from the right (+X)
                                                                     z 158  ⊙ screw (y −156.5)
  z 182 ┌flange┐   ┌──────── 17HS4401 ──────┐ x 125…165               │ tab
  z 158 ●══════════│screw (integrated)       │═ tail                   │
        │tab+nut│  └──────────────┬─────────┘                 z 132 ──┼──────────────────────────┐
        │  ◣    │ cradle slab ────┤ right wall x 145…150             │ plate: 35 high, rests on  │
  z 107 │  bar ─┴── support 45° ──┤ ┌plate┐ (in the wall's notch)    │ the lift, face pushes B   │
  z  97 └───────────────────────────┴─────┘                   z 97 ─┴───────────────────────────┘
        x 112    121           145   150                           y −165  −146.5               −55
```

- **The carriage is the lift turned 90°:** the plate (35 × 92 × 5) is the lift's plate, the bar along the front
  strip its riser, the tab with the TR8 nut its nut tab. One printed part, no rods.
- **What keeps the nut from turning:** the carriage's weight. Its centre of mass hangs 30 mm behind the screw, so the
  plate presses its lower edge onto the lift (then onto the divider). That holds ~7 mNm (23 g printed). The screw's
  drag tries to lift the plate while pushing, at ~2 mNm per newton of push: the carriage only tips at ~3 N, and a pile
  of 30 cards needs ~0.2 N. If something jams, the plate rides up over it instead of stalling.
- **Catching the bottom card:** the plate rests *on* the lift, so its lower edge sits at the lift's top, below the
  bottom card's edge. A 0.8 mm chamfer on that edge presses the card down instead of sliding over it. The lift's
  height still matters (calibrate it once, see "Untested assumptions"): if the lift is off by e, the plate tilts and
  its front end lifts by ~0.9 e.
- **Where it runs:** three things fix the layout. The divider block (z 66…96) spans the full depth, so everything that
  crosses it stays above z 97. The lift's riser (x 98…126, y −54…−50) rises to z 144 while pushing, so nothing may
  sit behind the cards. The cards cover y −142…−54, and the arm drops cards onto H from above. So the screw, the
  tab and the bar run in the **front strip** (y −165…−147); only the plate crosses the card area.
- **Motor:** a 17HS4401 with integrated TR8x8 screw (no coupling), half in the box (x 125…165). Its face is screwed
  to a flange of the box's **cradle**, its front part rests on a slab, and the slab sits on a support that grows 45°
  out of the right wall's inner face. The motor sits above the plate's home (z 137…179), the support stays in the
  front strip above the bar. The screw runs to the left wall and turns in a plain hole there.
- **Home:** the plate sits in a notch in the right wall, face flush with the inner face (x 145), resting on the notch
  floor 0.3 below the lift's push height. So it can only step *down* onto the lift. The tab's nut flange stops 0.5 mm
  before the cradle flange: home against it at low current (TMC2209 StallGuard, if the board has it, still undecided)
  or with one of the 4 limit switches on the shopping list.
- **Push:** lift plate top to **z 97.04** (`push_lift` = divider top + 1). The plate travels **73 mm** (x 145 → 72,
  ~9 screw turns), stepping down 1 mm from the lift onto the divider; at the end it stands on the divider and the
  pile lies at x 9…72, fully over H's chute, and drops. Then back home with the lift still up: a 1.5 mm chamfer on the
  plate's return side ramps it back over the lift's edge. Then lower the lift.
- **Direction:** the TR8x8 is right-handed. Which way "positive steps" turn depends on the wiring: set DIR in the firmware.
- ⚠️ The pile drops 30 mm (z 97 → H's top at ~66). It lands between the left wall and the divider, 9 mm wider than a
  card. Test with a real pile; if it lands skewed, try a slower push first.
- Pile B may be at most 12 mm high (≈ 37 cards) when pushed: above that it reaches the cradle and the motor.

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
| `pusher.scad` | Pusher carriage: plate, bar, nut tab | as placed, bottom down |
| `make_copy.py` | `ref/` (box, lift, 4023 from `dispenser.stl`) + `dispenser-roller.stl` (box with cradle and cuts, roller, pusher) | – |
| `check_pusher.py` | Collisions over the pusher's stroke with the lift at push height, and of the lift (4 heights) with the pusher at home (lift rebuilt from measured boxes) | – |

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
| 1 | 17HS4401 with integrated TR8x8 lead screw + TR8x8 brass nut | ⚠️ Not the motor + coupling + screw set on the shopping list (see its note) |
| 3 | M3×8 socket head | Motor face to the cradle flange (the 4th hole would cut the flange's 45° underside) |
| 2 | M3×8 | Nut flange to the tab (only the 2 holes above and below the screw) |

⚠️ **Cut the lead screw to 129 mm from the motor face** (hacksaw, then file a chamfer so it enters the nut cleanly).
It ends 1 mm inside the left wall's outer face.

Screws go into 2.5 mm pilot holes (self-tapping into PETG).

## Print (Bambu X2D)

- PETG, 4 walls, 15 % infill. Drums, motor gear and gates at 0.12–0.16 mm layers (fits, teeth, the gap step);
  frame and front plate at 0.2.
- **No supports.** Horizontal holes are teardrops, the drum grooves are teardrop-shaped and the lower bearing seat ends in a 45° cone.
- The gate prints on its side so its gap step lies in the XY plane, where the printer is most accurate.
- Pusher carriage: as it sits in the box, bottom down: the plate's lower edge, the one that slides, comes out of the
  bed flat. The nut hole is a teardrop. 0.2 mm layers.

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

**Pusher** (after Dev's changes to the box):

1. **Nut:** flange onto the tab's right face (towards the motor), body through the tab, 2× M3×8.
2. **Carriage in:** lift down. Slide the carriage in from the front: plate into the notch, bar into the low part.
3. **Motor in from the right:** screw first through the wall's motor passage and the flange's hole. Where it meets the
   nut, hold the motor and turn the screw by hand to thread it through; keep going until the screw end sits in the
   left wall's hole and the boss sits in the flange.
4. **Screw the motor on:** 3× M3×8 through the flange into the motor face (from the left, ball-end hex key over the box).
5. **Check:** turn the screw by hand with the lift at push height. The plate must slide on the lift, step down onto the
   divider, and come back over the lift's edge and into the notch without catching.

## Tuning the gap

1. Measure `card_t` (20 cards ÷ 20) and set it in `params.scad`. Everything follows from it: belt height, frame, gates.
2. If 2 cards pass or the bottom card jams, reprint only the gates, e.g. `parts/cad build dispenser gate -D gate_gap=0.4`,
   and swap them. That takes seconds, no screws.

## Dev: needed in Onshape

- **4 holes Ø3.4 in the box floor** at (x, y) = (20, −32), (60, −32), (20, −149), (60, −149).
- Keep free for the camera: the bed has a window at x 10…46, y −74…−56 (between the back edge of the card and the first belt).
- **Pusher notch** in the right wall, through, open to the front edge: y −165…−54 at z 96.74…107.54 (bar), plus
  y −147…−54 up to z 133.04 (plate). ⚠️ Printed upright, its top is a 93 mm bridge.
- **Motor passage** through the right wall: y −165…−135.05, z 136.59…179.49 (the motor's square + 0.3).
- **Cradle** on the right wall's inner face (exact geometry: `case_add()` in `lib.scad`, the box in
  `dispenser-roller.stl` has it):
  - Flange x 121…125, y −178.45…−135.05, z 132.59…182.49; its underside rises 45° from y −148 to the back edge.
    Hole Ø22.6 for the boss at (y, z) = (−156.5, 158.04); 3 holes Ø3.4 at (−172, 142.54), (−172, 173.54), (−141, 173.54).
  - Slab x 122…144.5, y −177.95…−147.5, z 133.59…136.59 (the motor's front part rests on it).
  - Support under the slab: hull down to a line on the wall's inner face at z 109.59 (y −165…−147.5): 45° along X.
- **Screw hole Ø8.6 through the left wall** at (y, z) = (−156.5, 158.04).
- The spare 17HS4401 + screw + nut at x 354…396 are now the pusher's: the copy drops them and places its own.
- The motor sticks out 15 mm to the right (x 165, the screw's tail to 169) and 13 mm in front of the box (y −178).

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
- Pusher: the plate rests on the lift by its weight (~7 mNm holding torque, estimated for 23 g printed; tips at ~3 N
  push). The lift's push height has to be calibrated once: push a single card lying at the front (y ≈ −140), then one
  at the back (y ≈ −60), and change the height in 0.05 mm steps until both are caught. Bowed cards may still slip under the plate's front end; if they do, the fix is a comb
  (slots in the lift plate, Dev's part) so the plate's edge can dip below the lift top.
- Pusher: the screw turns in a plain Ø8.6 hole in the left wall (PETG on steel, slow, light load). The nut's
  dimensions (TR8 nut Ø22 flange on a Ø16 bolt circle) are taken from Dev's lift model; the integrated motor's
  tail (3.8 mm out of the back) too.
