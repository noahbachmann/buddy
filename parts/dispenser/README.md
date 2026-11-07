# Card dispenser: roller (belt conveyor) and pusher

The box (`dispenser.stl`, Dev's Onshape model) holds Buddy's hand in two piles: **H** on the left, face down over a
camera, and **B** on the lift on the right (Phase 5 plan: hand trays H and B, `feed` / `take` / `stack`). This module adds
the roller under H and the pusher on the right wall. The roller moves the **bottom card** of H either way:

- **take** (−X): out through the open left side onto the belts outside the box, where the arm picks it up.
- **feed** (+X): through the divider's slot onto the lift pile B.

The pusher moves **all of B** back: **stack(B onto H)** (−X): the lift raises B to the pusher, which pushes the pile
over the divider into H's chute, where it drops onto H.

`dispenser.stl` stays untouched. `dispenser-roller.stl` is the copy with the roller and the pusher in place, the 4023
turned and the box changed as Dev needs to change it (below: notch, wall extension, holes). It also holds bought parts as
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
  seen from the front (−Y), carriage at home
                                                                   right wall x 145…150
  z 154 ●══════ screw (TR8x8) ══════════╪coupling╪═ shaft ═│17HS4401 (x 150…190, on the wall's outer face)
        │nut│tab                        x 113…138          │
        │   │◣ gusset                                 ┌────┤
  z 132 │   │                                         │    │ plate: 35 high, rests on the lift, face pushes B
  z 107 │   └──────────── bar ─────────────────────── │    │
  z  97 └─────────────────────────────────────────────┴────┘ ← in the wall's notch at home (4 deep, 1 mm skin)
        x 102.5                                      145  149
```

- **The carriage is the lift turned 90°:** the plate (35 × 92 × 4) is the lift's plate, the bar along the front
  strip its riser, the tab with the TR8 nut its nut tab. One printed part, no rods.
- **What keeps the nut from turning:** the carriage's weight. Its centre of mass hangs 26 mm behind the screw, so the
  plate presses its lower edge onto the lift (then onto the divider). That holds ~5 mNm (21 g printed). The screw's
  drag tries to lift the plate while pushing, at ~2 mNm per newton of push: the carriage only tips at ~2.5 N, and a
  full pile (30 cards) needs ~0.2 N. If something jams, the plate rides up over it instead of stalling.
- **Catching the bottom card:** the plate rests *on* the lift, so its lower edge sits at the lift's top, below the
  bottom card's edge. A 0.8 mm chamfer on that edge presses the card down instead of sliding over it. The lift's
  height still matters (calibrate it once, see "Untested assumptions"): if the lift is off by e, the plate tilts and
  its front end lifts by ~0.9 e.
- **Where it runs:** three things fix the layout. The divider block (z 66…96) spans the full depth, so everything that
  crosses it stays above z 97. The lift's riser (x 98…126, y −54…−50) rises to z 144 while pushing, so nothing may
  sit behind the cards. The cards cover y −142…−54, and the arm drops cards onto H from above. So the screw, the
  coupling, the tab and the bar run in the **front strip** (y −165…−147); only the plate crosses the card area.
- **Pile height:** up to 35 mm (30 loose cards). The plate is as high as that pile; the motor and its lower screw
  holes sit above it, which sets the screw at z 154.
- **Motor:** a 17HS4401 screwed flat onto the right wall's outer face (4× M3 from inside the box). The wall is
  extended 15 mm forward around it, because the motor is wider than the front strip. The coupling (Ø19 × 25)
  joins the 5 mm shaft and the screw inside the box; the screw runs to the left wall and turns in a plain hole there.
- **Home:** the plate sits in a notch in the right wall, face flush with the inner face (x 145), resting on the notch
  floor 0.3 below the lift's push height, so it can only step *down* onto the lift. The notch is 4 mm deep: the last
  1 mm of wall is the home stop. Home against it at low current (TMC2209 StallGuard, if the board has it, still
  undecided) or with one of the 4 limit switches on the shopping list.
- **Push:** lift plate top to **z 97.04** (`push_lift` = divider top + 1). The plate travels **73 mm** (x 145 → 72,
  ~9 screw turns), stepping down 1 mm from the lift onto the divider; at the end it stands on the divider and the
  pile lies at x 9…72, fully over H's chute, and drops. Then back home with the lift still up: a 1.5 mm chamfer on the
  plate's return side ramps it back over the lift's edge. Then lower the lift.
- **Direction:** the TR8x8 is right-handed. Which way "positive steps" turn depends on the wiring: set DIR in the firmware.
- ⚠️ The pile drops 30 mm (z 97 → H's top at ~66). It lands between the left wall and the divider, 9 mm wider than a
  card. Test with a real pile; if it lands skewed, try a slower push first.

## Lift motor

With a coupling on the lift's motor (shaft top at z 69), the coupling would sit at z 56…82, right where the lift's
nut hangs at the bottom of its travel (z 47.5). The lift could then not go below z ~54, which leaves ~12 mm between
the lift plate and the divider slot, where the roller feeds every card onto pile B. So the motor moves under the box:
face against the floor's underside, boss in the floor's hole, 4× M3 from inside. The coupling then sits at z
11.5…36.5 and the lift keeps its full travel (plate top down to z 19; a 35 mm pile needs z 30.5). The motor hangs
40 mm below the floor: the box needs feet of at least 42 mm. (The other option, the motor on the coming ceiling with
the screw hanging down, works as well; the copy shows the floor version.)

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
| `make_copy.py` | `ref/` (box, lift, 4023 from `dispenser.stl`) + `dispenser-roller.stl` (box with Dev's changes, roller, pusher, lift motor under the floor) | – |
| `check_pusher.py` | Collisions over the pusher's stroke with the lift at push height and a full pile, and of the lift (4 heights) with the pusher at home and its own motor and coupling (lift rebuilt from measured boxes) | – |

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

**Pusher and lift motor**

| Qty | Part | Note |
|---|---|---|
| 2 | 17HS4401 + Purecrea flexible coupling 8/5 + TR8x8 lead screw + TR8x8 brass nut | The two sets on the shopping list: pusher and lift |
| 8 | M3×8 socket head | 4 per motor: through the 5 mm right wall / box floor into the motor face (holes ~4.5 deep), from inside |
| 2 | M3×8 | Pusher nut flange to the tab (only the 2 holes above and below the screw) |

⚠️ **Cut the pusher's screw to 130 mm** (hacksaw, then file a chamfer so the nut starts cleanly). It reaches from the
middle of the coupling to 1 mm inside the left wall's outer face. Cut the end that goes into the coupling. The lift's
screw runs from the coupling to the box top: 176 mm.

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

1. **Motor:** coupling on the shaft (its middle at the shaft's tip, 24 mm from the face), tighten the 5 mm side.
   Motor onto the right wall's outer face, boss in the wall's hole, 4× M3×8 from inside the box.
2. **Nut:** flange onto the tab's right face (towards the motor), body through the tab, 2× M3×8.
3. **Carriage in:** lift down. Slide the carriage in from the front: plate into the notch, bar into the low part.
4. **Screw in:** through the left wall's hole from outside, thread it through the nut by turning it, into the coupling.
   Tighten the 8 mm side through the open front.
5. **Check:** turn the screw by hand with the lift at push height. The plate must slide on the lift, step down onto the
   divider, and come back over the lift's edge and into the notch without catching.

## Tuning the gap

1. Measure `card_t` (20 cards ÷ 20) and set it in `params.scad`. Everything follows from it: belt height, frame, gates.
2. If 2 cards pass or the bottom card jams, reprint only the gates, e.g. `parts/cad build dispenser gate -D gate_gap=0.4`,
   and swap them. That takes seconds, no screws.

## Dev: needed in Onshape

- **4 holes Ø3.4 in the box floor** at (x, y) = (20, −32), (60, −32), (20, −149), (60, −149).
- Keep free for the camera: the bed has a window at x 10…46, y −74…−56 (between the back edge of the card and the first belt).
- **Pusher notch** in the right wall, 4 mm deep from the inside (1 mm of wall stays), open to the front edge:
  y −165…−54 at z 96.74…107.54 (bar), plus y −147…−54 up to z 133.04 (plate).
- **Right wall extended forward** around the pusher motor: x 145…150, to y −179.65, z 130.89…177.19, the underside
  at 45° back to the wall's front edge (prints upright).
- **Holes through the right wall** for the pusher motor: Ø22.6 for the boss at (y, z) = (−156.5, 154.04), 4× Ø3.4 at
  (−172, 138.54), (−141, 138.54), (−172, 169.54), (−141, 169.54).
- **Screw hole Ø8.6 through the left wall** at (y, z) = (−156.5, 154.04).
- **Lift motor under the floor:** in the floor at (x, y) = (111.9, −26) a hole Ø22.6 for the boss and 4× Ø3.4 at
  (96.4, −41.5), (127.4, −41.5), (96.4, −10.5), (127.4, −10.5). Feet ≥ 42 mm under the box.
- The spare 17HS4401 + screw + nut at x 354…396 are now the pusher's, and the lift's motor and screw move under the
  floor: the copy drops them and places its own.
- The pusher motor sticks out 40 mm to the right (x 190) and 13 mm in front of the box (y −178), the wall extension 15 mm.

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
- Pusher: the plate rests on the lift by its weight (~5 mNm holding torque, estimated for 21 g printed; tips at ~2.5 N
  push). The lift's push height has to be calibrated once: push a single card lying at the front (y ≈ −140), then one
  at the back (y ≈ −60), and change the height in 0.05 mm steps until both are caught. Bowed cards may still slip
  under the plate's front end; if they do, the fix is a comb (slots in the lift plate, Dev's part) so the plate's
  edge can dip below the lift top.
- Pusher: the screw turns in a plain Ø8.6 hole in the left wall (PETG on steel, slow, light load). Coupling (Ø19 × 25)
  and nut dimensions (TR8 nut Ø22 flange on a Ø16 bolt circle) are taken from Dev's model and the usual sizes.
