# Card dispenser: roller (belt conveyor) and pusher

The box (`dispenser.stl`, Dev's Onshape model) holds Buddy's hand in two piles: **H** on the left, face down over a
camera, and **B** on the lift on the right (Phase 5 plan: hand trays H and B, `feed` / `take` / `stack`). This module adds
the roller under H and the pusher on the right wall. The roller moves the **bottom card** of H either way:

Compared with `dispenser.stl`: the roller's back plate and the lift's screw sit 8.5 mm further back (`shift`, the 4023
in the back wall). The cards sit 18.5 mm further back (y −123.5…−35.5, their back edge 1 mm in front of the drum gear).
The lift is a printed part of this module, compact enough for that. The roller is 10 mm shorter, and the box
18.5 mm shorter at the front (y −146.5). All coordinates here are the new ones.

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

- **Belts:** three NBR O-rings (80 × 3 mm) at y −59.5 / −79.5 / −99.5, running on a printed drive drum and idler drum. They are
  stretched 7 % (279.0 mm path, 260.8 mm free), which gives tension without a tensioner. The pile rests on the belts,
  rubber on card, so only the bottom card moves.
- **Gates:** a bar across the card path, `gap` = 1.5 card thicknesses (0.48 mm) above the belt top. The bottom card fits
  under it, the second one doesn't. One gate stands on each side of the pile. Both sit at the height of the divider's
  upper block (z 66.04), so the belt top is `66.04 − gap`. The divider's 1.04 mm slot is now only clearance: the right gate
  does the separating, and it can be swapped to tune it.
- **Right gate's lip:** between the drive drum's top (x 62) and the divider (x 72) the card has 10 mm without support,
  and its leading edge would droop into the gap and hit the divider under the slot. The right gate has a lip under the
  card path there: flat top 0.5 mm below the belt top (z 65.06, just above the divider's lower part at z 65), drum
  side following the belts at 0.6 mm clearance. It runs from the front leg to the back leg. Over the drum gear
  (y −34.5…−27.5) its underside clears the gear's tips by 0.6 mm.
- **Slit ends:** the divider's slot is filled at both ends (Dev's change, below). The fill and the gate's back block
  form one 45° funnel. It narrows from the gate's entry side to the card's width + 1 mm per side at the divider's
  right face (y −124.5…−34.5). A card up to ~3 mm (front: the gate's front leg) or ~1 mm (back: the drum gear) off centre is led
  into it.
- **Bed:** the belts slide on a flat bed. One O-ring strand carries only ~2.5 N, so a pile of 20 cards would sag a free
  span by ~1.4 mm and the cards would sit on the bed instead of the rubber.
- **Drive:** the 4023 is turned 90° (shaft to the front, connector down) and sits behind the back plate, in a window
  through the back wall (y −20.5…3, 3 mm out at the back). It rests on a block of the box (the hold) under it, and
  its connector hangs in a 20 mm channel through the hold and the floor: the cable leaves under the box. A 20T
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
  crosses it stays above z 97. The lift's back stop (y −35.5…−31.5) rises to z 134 while pushing, so nothing may
  sit behind the cards. The cards cover y −123.5…−35.5, and the arm drops cards onto H from above. So the screw,
  the coupling, the tab and the bar run in the **front strip** (y −146.5…−128); only the plate crosses the card area.
- **Pile height:** up to 35 mm (30 loose cards). The plate is as high as that pile; the motor and its lower screw
  holes sit above it, which sets the screw at z 154.
- **Motor:** a 17HS4401 screwed flat onto the right wall's outer face (4× M3 from inside the box). The wall is
  extended 15 mm forward around it, because the motor is wider than the front strip. A wedge under the motor (16 mm
  wide, 45° underside) carries it, so it doesn't hang on its screws alone. The coupling (Ø19 × 25)
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

## Lift

- **Motor under the floor:** with a coupling, a motor inside the box would put the coupling (z 56…82) where the nut
  has to go. So the motor sits under the box: face against the floor's underside, boss in the floor's hole, M3 from
  inside. Its axis is at y −17.5, so it reaches under the back wall and its 2 back screw holes are out of reach. It's
  held by the 2 front screws and the boss (the load is the lift's few hundred grams). The coupling sits at z
  11.5…36.5. The motor hangs 40 mm below the floor: the box needs feet of at least 42 mm.
- **Lift (printed, `lift.scad`):** plate 67 × 96 × 4 mm (x 77.44…144.44, 0.44 / 0.56 to the divider and the right
  wall: that keeps it from turning). Back stop 4 mm thick (y −35.5…−31.5), 37 mm high (pile + 2): full plate width at
  the plate, narrowing upwards along concave curves to 28 mm at the top. Nut tab right behind it with 2 gussets, the
  nut's flange on top, 2× M3. The tab is low: at the lowest plate position (z 29.56, a 35 mm pile under the slot) the
  nut clears the coupling by 0.5 mm. The nut flange keeps 1.5 mm to the back wall.
- **Front border (box):** a 4 mm wall 0.5 mm in front of the lift plate (y −132…−128), from the divider to the right
  wall, floor to z 65 with a ^ ridge on top. It keeps pile B from sliding out of the open front while the lift is low.

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
| `gate-left.scad` | Left gate | on its side |
| `gate-right.scad` | Right gate with the lip under the card path | on its side, divider side down |
| `pusher.scad` | Pusher carriage: plate, bar, nut tab | as placed, bottom down |
| `lift.scad` | Lift: plate, back stop, nut tab (replaces Dev's) | as placed, plate down |
| `make_copy.py` | `ref/` (box, lift, 4023 from `dispenser.stl`) + `dispenser-roller.stl` (box with Dev's changes, roller, pusher, lift, lift motor under the floor) | – |
| `check_pusher.py` | Collisions over the pusher's stroke with the lift at push height and a full pile, and of the lift with its nut (lowest, 2 between, push height) with the box, the pusher at home and its own motor and coupling | – |

```bash
cd parts/tools && uv run python ../dispenser/make_copy.py    # after any change, also regenerates ref/
cd parts/tools && uv run python ../dispenser/check_pusher.py # after a pusher, lift or box change: must print 0 overlapping
```

## Parts

| Qty | Part | Note |
|---|---|---|
| 3 | NBR O-ring 80 × 3 mm (ID × cord), 70 Shore A | The belts. ⚠️ Different size: change `oring_id`, the drum distance follows |
| 4 | MR105ZZ bearing, 5 × 10 × 4 | Pressed into the drum ends |
| 2 | Steel rod Ø5 mm, 112 mm | Silver steel, cut and deburred. Fixed: the drums turn on their bearings |
| 3 | M3×6 socket head | 4023 to the back plate (the 4th hole would cut into the drive rod's hole) |
| 1 | M3×4 set screw + M3 nut | Motor gear onto the shaft's flat |
| 2 | M3×6 + M3 washer | Rod keepers on the front plate (the washer covers the rod end) |
| 3 | M3×12 | Front plate into the rib ends |
| 4 | M3×12 | Frame to the box floor, from below |

**Pusher and lift motor**

| Qty | Part | Note |
|---|---|---|
| 2 | 17HS4401 + Purecrea flexible coupling 8/5 + TR8x8 lead screw + TR8x8 brass nut | The two sets on the shopping list: pusher and lift |
| 6 | M3×8 socket head | Through the 5 mm right wall (4, pusher) / box floor (2, lift) into the motor face (holes ~4.5 deep), from inside |
| 4 | M3×8 | Nut flanges to the tabs, pusher and lift (only 2 holes each, beside the screw) |

⚠️ **Cut the pusher's screw to 130 mm** (hacksaw, then file a chamfer so the nut starts cleanly). It reaches from the
middle of the coupling to 1 mm inside the left wall's outer face. Cut the end that goes into the coupling. The lift's
screw runs from the coupling to the box top: 176 mm.

Screws go into 2.5 mm pilot holes (self-tapping into PETG).

## Print (Bambu X2D)

- PETG, 4 walls, 15 % infill. Drums, motor gear and gates at 0.12–0.16 mm layers (fits, teeth, the gap step);
  frame and front plate at 0.2.
- **No supports.** Horizontal holes are teardrops, the drum grooves are teardrop-shaped and the lower bearing seat ends in a 45° cone.
- The gates print on their side so the gap step lies in the XY plane, where the printer is most accurate. The right
  gate's lip grows from its divider-side face without overhang.
- Pusher carriage: as it sits in the box, bottom down: the plate's lower edge, the one that slides, comes out of the
  bed flat. The nut hole is a teardrop. 0.2 mm layers.

## Assembly

The 4023 sits in the back wall, so the frame goes into the box first and the motor comes in from behind.

1. **Bearings:** press 2 MR105 into each drum, pushing only on the outer race.
2. **Frame into the box:** its step against the left edge of the box floor, 2× M3×12 from below into the back plate.
3. **Motor:** cable plugged in. Push the 4023 through the back wall's window from behind, sliding on the hold,
   connector down in the channel, cable out under the box. Boss into the plate's recess, 3× M3×6 from the front;
   the heads sink into the plate. The drum gear will run over them.
4. **Motor gear:** on the shaft, flush with the drum gear's plane (7 mm in front of the motor face). Tighten the set screw on the flat.
5. **Drums:** push the rods into the back plate (blind holes), then slide the drums on with the drive drum's gear at the back, meshing.
6. **Belts:** slide the 3 O-rings over both drums from the front, into the grooves. They pass over the bed and the ribs.
7. **Front plate:** rod ends into its holes, 3× M3×12 into the rib ends, 2 keeper screws with washers, 2× M3×12 from
   below through the floor.
8. **Gates:** press them into their sockets from above: the left one under the left wall, the right one (with the lip)
   in front of the divider. The lip passes the drum on its way down.

**Lift:** motor under the floor (boss into the floor's hole, 2× M3×8 from inside), coupling on the shaft inside the
box (z 11.5…36.5), tighten the 5 mm side. Nut flange onto the lift's tab from above, 2× M3×8. Outside the box, turn the
screw through the nut (the lift can't turn once it's in). Lower lift and screw in together, screw end into the
coupling, tighten the 8 mm side.

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
2. If 2 cards pass or the bottom card jams, reprint only the gates, e.g. `parts/cad build dispenser gate-left gate-right -D gate_gap=0.4`,
   and swap them. That takes seconds, no screws.

## Dev: needed in Onshape

- **Box 18.5 mm shorter:** front edge at y −146.5 instead of −165 (floor, side walls, divider).
- **Lift:** delete yours; the printed one from `lift.scad` replaces it.
- **Front border for pile B:** wall y −132…−128, x 77…145 (divider to right wall), z 5…65, its top a ^ ridge
  (45° both sides, peak at z 65).
- **4023 in the back wall:** window through the wall at x 28.55…71.45, z 22.46…65.06 (the motor + 0.3, its floor
  flush with the motor's underside). Hold under the motor: a block x 28.85…71.15, y −20.2…3 (from 0.3 behind the
  frame's back plate to the motor's back end, 3 mm out behind the wall), z 0…22.46. Cable channel x 40…60,
  y −6.6…3 (open at the back), from the hold's top down through the floor.
- **4 holes Ø3.4 in the box floor** at (x, y) = (20, −23.5), (60, −23.5), (20, −130.5), (60, −130.5).
- Keep free for the camera: the bed has a window at x 10…46, y −55.5…−37.5 (between the back edge of the card and the first belt).
- **Divider slot filled at both ends** (z 65…66.04, x 72…77), leaving the card's width open. Each fill ends in a 45°
  face toward the card. Front: from the front edge (y −146.5) to the line (72, −129.5)→(77, −124.5). Back: from the
  back wall (y −5) to the line (72, −29.5)→(77, −34.5). Seen from above, the opening is 100 mm wide at x 72 and 90 mm
  at x 77. This also ties the divider's upper block to the lower part.
- **Pusher notch** in the right wall, 4 mm deep from the inside (1 mm of wall stays), open to the front edge:
  y −146.5…−35.5 at z 96.74…107.54 (bar), plus y −128.5…−35.5 up to z 133.04 (plate).
- **Right wall extended forward** around the pusher motor: x 145…150, to y −161.15, z 130.89…177.19, the underside
  at 45° back to the wall's front edge (prints upright).
- **Pusher motor hold:** a wedge on the right wall's outer face under the motor: y −146…−130, top at z 132.89 (the
  motor's underside) from x 150 to 190, 3 mm thick there, underside at 45° down to the wall at z 88.89.
- **Holes through the right wall** for the pusher motor: Ø22.6 for the boss at (y, z) = (−138, 154.04), 4× Ø3.4 at
  (−153.5, 138.54), (−122.5, 138.54), (−153.5, 169.54), (−122.5, 169.54).
- **Screw hole Ø8.6 through the left wall** at (y, z) = (−138, 154.04).
- **Lift motor under the floor:** in the floor at (x, y) = (111.9, −17.5) a hole Ø22.6 for the boss and 2× Ø3.4 at
  (96.4, −33), (127.4, −33) (the back 2 would be under the wall). Feet ≥ 42 mm under the box; the motor reaches
  3.65 mm past the back wall.
- The spare 17HS4401 + screw + nut at x 354…396 are now the pusher's, and the lift's motor and screw move under the
  floor: the copy drops them and places its own.
- The pusher motor sticks out 40 mm to the right (x 190) and 13 mm in front of the box (y −159.15), the wall extension 15 mm.

## ⚠️ Open: feed stops short

A fed card is driven only while it sits on the belt. Its trailing edge stops over the drive drum (x 62), so the card
ends at x 62…125, while the divider ends at x 77. The last 15 mm still sit in the slot and under the right gate, on
the lip. The lip keeps them 0.5 mm below the belt top, so the next card slides over them instead of into their edge,
but then two cards sit in the slot. No roller left of the divider can push further. Options:

1. **Software, test first:** feed fast so the card slides on over pile B until it hits the right wall (x 145). It then
   ends at x 82…145, clear of the divider and squared against the wall. That needs ≳ 0.35 m/s at the belt (~6 drum
   turns/s, ~300 rpm at the motor; card on card μ ≈ 0.3, 20 mm to slide). ⚠️ A pancake stepper should reach that
   without load. Test whether it does with the card.
2. **Hardware:** a driven pinch roller on the lift side behind the slot, geared from the drum through the gear zone
   behind the cards (y −27.5…−34.5, now only 1 mm behind them). It would have to get out of the way when the lift pushes B back over to the left.

## ⚠️ Untested assumptions

- `card_t` = 0.32 mm is a guess (sleeves change everything).
- ⚠️ Pile H's back edge is only 1 mm in front of the drum gear, whose tips reach 0.6 mm above the belts (y −34.5…−27.5,
  x ~59…65). Nothing positions H in Y: a card placed more than 1 mm toward the back rubs on the turning gear. If that
  happens, H needs a back stop (e.g. a thin wall on the frame over the gear).
- The MR105 press fit (`press`), the 5.2 mm rod holes and the printed gear mesh (`gear_slack`, `gear_backlash`) need a test print.
- NBR on the card back is assumed to grip better than card on card. If the second card comes along, lower the gap first.
- The 4023's model (connector side, shaft flat at 2.0 mm from the axis) is taken from `dispenser.stl` as drawn.
- Pusher: the plate rests on the lift by its weight (~5 mNm holding torque, estimated for 21 g printed; tips at ~2.5 N
  push). The lift's push height has to be calibrated once: push a single card lying at the front (y ≈ −121.5), then one
  at the back (y ≈ −41.5), and change the height in 0.05 mm steps until both are caught. Bowed cards may still slip
  under the plate's front end; if they do, the fix is a comb (slots in the lift plate) so the plate's
  edge can dip below the lift top.
- Pusher: the screw turns in a plain Ø8.6 hole in the left wall (PETG on steel, slow, light load). Coupling (Ø19 × 25)
  and nut dimensions (TR8 nut Ø22 flange on a Ø16 bolt circle) are taken from Dev's model and the usual sizes.
