# Card dispenser: roller (belt conveyor) and pusher

The box (`dispenser.stl`, Dev's Onshape model) holds Buddy's hand in two piles: **H** on the left, face down over a
camera, and **B** on the lift on the right (Phase 5 plan: hand trays H and B, `feed` / `take` / `stack`). This module adds
the roller under H and the pusher on the right wall. The roller moves the **bottom card** of H either way:

Compared with `dispenser.stl`: the roller's back plate and the lift's screw sit 8.5 mm further back (`shift`, the 4023
in the back wall). The cards are sleeved, 66.5 × 91 mm (`card`), and sit at y −131.4…−40.4: the lift is a printed part
of this module, its slope right in front of the nut. The box is 10.6 mm shorter at the front (y −154.4). All
coordinates here are the new ones.

**The case is printed from this module** (`case.scad`): Dev's box (`ref/box.stl`, cut out of `dispenser.stl`) with every
change listed under "Changes to Dev's box", plus feet. Two borders slide in from the front as separate parts.

- **take** (−X): out through the open left side onto the belts outside the box, where the arm picks it up.
- **feed** (+X): through the divider's slot onto the lift pile B.

The pusher moves **all of B** back: **stack(B onto H)** (−X): the lift raises B to the pusher, which pushes the pile
over the divider into H's chute, where it drops onto H.

`dispenser.stl` stays untouched. `dispenser-roller.stl` is the copy with the printed case, the roller, the pusher and
the lift in place and the 4023 turned. It also holds bought parts as stand-ins (motors, pins, screw, nut, O-rings).
`make_copy.py` regenerates it.

## How the roller works

```
            left gate (x −5…0, under the left wall)      right gate (x 67.5…72, box)  divider slot
                 │  pile H (x 0.5…67)                          │                  │
   ──────────────▼────────────────────────────────────────────▼─────────────────▼──────── z 66.04
   ═══════════════════════ belts (3 O-rings), top at z 65.49 ══════════════════   lift / B
  (idler drum x −55.7)          bed: belts slide on it            (drive drum x 62) ◄─ 20:16 ◄─ 4023
```

- **Belts:** three NBR O-rings (80 × 3 mm) at y −65.9 / −85.9 / −105.9, running on a printed drive drum and idler drum. They are
  stretched 7 % (279.0 mm path, 260.8 mm free), which gives tension without a tensioner. The pile rests on the belts,
  rubber on card, so only the bottom card moves.
- **Gates:** a bar across the card path, `gap` = 0.55 mm above the belt top (Dev's choice: a pressed card is ≤ 0.5, two ~1.0; 0.05 mm over `card_t`). The bottom card fits
  under it, the second one doesn't. One gate stands on each side of the pile. Both sit at the height of the divider's
  upper block (z 66.04), so the belt top is `66.04 − gap`. The left gate is printed and swappable. The right gate is
  part of the case: the divider's upper block extended 4.5 mm to the left (x 67.5…72), so its
  gap is fixed by the box and the frame (see "Tuning the gap").
- **Right gate's lip:** between the drive drum's top (x 62) and the divider (x 72) the card has 10 mm without support,
  and its leading edge would droop into the gap and hit the divider under the slot. The right gate has a lip under the
  card path there: flat top a card + 0.2 below the belt top (z 64.79; the case cuts the divider's lower part down to
  z 64.73 under the slot, Dev's is at 65), drum
  side clear of the drum gear's tips by 0.6 mm (r 9.6, the drums slide onto their pins under it). It runs between the
  frame plates and hangs from the gate's legs above them.
- **Slit ends:** the divider's slot is filled at both ends (case). The fill and the right gate's blocks
  form one 45° funnel. It narrows from the gate's entry side to the card's width + 1 mm per side at the divider's
  right face (y −132.4…−39.4). A card up to ~3 mm (front: the gate's front leg) or ~6 mm (back: the drum gear) off
  centre is led into it.
- **Bed:** the belts slide on a flat bed. One O-ring strand carries only ~2.5 N, so a pile of 20 cards would sag a free
  span by ~1.4 mm and the cards would sit on the bed instead of the rubber.
- **Drive:** the 4023 is turned 90° (shaft to the front, connector down) and sits behind the back plate, in a window
  through the back wall (y −20.5…3, 5 mm out at the back). It rests on a block of the case (the hold) under it, and
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
- **Into the box:** the right gate's legs and lip are in the way from above, so the frame slides in from the open left
  side (under the left wall and the right gate's legs, 0.3 mm above the plates), then the drums and the front plate
  slide on from the front (checked step by step against the box).

## How the pusher works

```
  seen from the front (−Y), carriage at home
                                                                   right wall x 143…146
  z 156 ●══════ screw (TR8x8) ══════════╪coupling╪═ shaft ═│17HS4401 (x 146…186, on the wall's outer face)
        │nut│tab                        x 110…134          │
        │   │◣ gusset                                 ┌────┤
  z 132 │   │                                         │    │ plate: 35 high, rests on the lift, face pushes B
  z 107 │   └──────────── bar ─────────────────────── │    │
  z  97 └─────────────────────────────────────────────┴────┘ ← in the wall's notch at home (through the wall, 2 mm skin)
        x 101.5                                      143  146
```

- **The carriage is the lift turned 90°:** the plate (35 × 94.5 × 3) is the lift's plate, the bar along the front
  strip its riser, the tab with the TR8 nut its nut tab. One printed part, no rods, every wall 3 mm: the bar is an
  upside-down U (two legs, a deck on top, open below, an end wall at the plate), the gusset two side walls, the tab a
  3 mm plate. The guide ridge runs inside the U.
- **What keeps the nut from turning:** the carriage's weight. Its centre of mass hangs 26 mm behind the screw, so the
  plate presses its lower edge onto the lift (then onto the divider). That holds ~5 mNm (21 g printed). The screw's
  drag tries to lift the plate while pushing, at ~2 mNm per newton of push: the carriage only tips at ~2.5 N, and a
  full pile (30 cards) needs ~0.2 N.
- **Guide against turning:** a ridge along X under the bar (front face y −150.4, z 96.04…99.04, flat 1 mm top, back at
  45°) on the tops of border H, the divider and border B, inside the bar's U (y −150.6…−138.9, 7 high; under the tab
  and the end wall a groove y −150.6…−145.2, 3.5 deep). Turning the plate's far end up moves the bar back (+Y): the groove's front wall meets the ridge 0.2 mm
  later, 58 mm below the screw, so the far end (104 mm behind it) rises ≤ ~0.4 mm (~0.5 with the borders' print
  tolerance) however hard the screw drags. Turning the other way is what stepping onto the divider does (the bar moves
  ~0.6 forward): 1.2 mm behind the ridge. Free in Z: the plate still rests on the lift by its weight, the guide only
  stops the rise. Each ridge's front face is set back 0.6 mm at its ends, so the groove passes the joints between the
  three parts. At a jam the plate no longer rides up: the motor pushes into it, so keep the current moderate (needed
  for PLA anyway) and watch the TMC2209's stall value (StallGuard over UART) as a jam alarm.
- **Catching the bottom card:** the plate rests *on* the lift, so its lower edge sits at the lift's top, below the
  bottom card's edge. A 0.8 mm chamfer on that edge presses the card down instead of sliding over it. The lift's
  height still matters (calibrate it once, see "Untested assumptions"): if the lift is off by e, the plate tilts and
  its front end lifts by ~0.9 e.
- **Where it runs:** three things fix the layout. The divider block (z 66…96) spans the full depth, so everything that
  crosses it stays above z 97. The lift's slope (from y −40.4 at the plate) rises to z 144 while pushing, so nothing may
  sit behind the cards. The cards cover y −131.4…−40.4, and the arm drops cards onto H from above. So the screw, the
  coupling, the tab and the bar run in the **front strip** (bar y −153.6…−135.9); only the plate crosses the card area.
- **Pile height:** up to 35 mm (30 loose cards). The plate is as high as that pile; the motor and its lower screw
  sits on the notch's skin above it (plate top + 0.5 + 2), which sets the screw at z 155.69.
- **Motor:** a 17HS4401 screwed flat onto the right wall's outer face (4× M3 from inside the box). The wall is
  extended 15 mm forward around it, because the motor is wider than the front strip. A wedge under the motor (16 mm
  wide, 45° underside) carries it, so it doesn't hang on its screws alone. The coupling (Ø19 × 25)
  joins the 5 mm shaft and the screw inside the box; the screw runs to the left wall, turns in a plain hole there and,
  200 mm and uncut, sticks 75 mm out of the box's left side.
- **Home:** the plate sits in a notch in the right wall, face flush with the inner face (x 143), resting on the notch
  floor 0.3 below the lift's push height, so it can only step *down* onto the lift. The notch is 3 mm deep, through the
  3 mm wall; a 2 mm skin outside it is the home stop. Home against it at low current (TMC2209 StallGuard, if the board has it, still
  undecided) or with one of the 4 limit switches on the shopping list.
- **Push:** lift plate top to **z 97.04** (`push_lift` = divider top + 1). The plate travels **71 mm** (x 143 → 72,
  ~9 screw turns), stepping down 1 mm from the lift onto the divider; at the end it stands on the divider and the
  pile lies at x 5.5…72, fully over H's chute, and drops. Then back home with the lift still up: a 1.5 mm chamfer on the
  plate's return side ramps it back over the lift's edge. Then lower the lift.
- **Direction:** the TR8x8 is right-handed. Which way "positive steps" turn depends on the wiring: set DIR in the firmware.
- **Into H:** the pile leaves the divider at x 5.5…72 and drops 30 mm (z 97 → H's top at ~66). Pile H has borders like
  pile B: front and back (left wall to the divider, z 70.5…96) with inner faces 0.5 mm off the
  cards at the bottom, leaning out ~10° to 4.4 mm at the top (the back one solid to the back wall, the front one the
  slide-in `border-h`); on the divider side, a wedge on the right gate from its
  inner face (x 67.5, z 69.6) up to the divider's top edge. It leads the pile 4.5 mm to the left, onto the belts
  between the gates. ⚠️ Test with a real pile; if it lands skewed, try a slower push first.

## Lift

- **Motor under the floor:** with a coupling, a motor inside the box would put the coupling (z 56…82) where the nut
  has to go. So the motor sits under the box: face against the floor's underside, boss in the floor's hole, M3 from
  inside. Its axis is at (109.8, −16.9) (Dev's nut centre, 2 mm left with the right wall), so it reaches under the back wall and its 2 back screw holes are out of reach. It's
  held by the 2 front screws and the boss (the load is the lift's few hundred grams). The coupling sits at z
  13.5…38.5. The motor hangs 40 mm below the floor: the case stands on two rails 42 mm high (below).
- **Lift (printed, `lift.scad`):** seen from the side, Dev's nut tab around the screw at the top (47 mm above the plate
  top), then one slope down to the plate. Plate 67 wide, 3 thick (x 75.44…142.44, 0.44 / 0.56 to the
  divider and the right wall: that keeps it from turning), its top 95 mm from the front edge to the slope's foot: the
  card + 4 mm in front, as Dev's. The slope starts at the cards' back
  edge (y −40.4) and leans back 8 mm up to the tab (~10°), so a card dropping onto a low lift slides down into place;
  it is a 3 mm wall (along Y), its back face leaning like the front;
  from the front it narrows upwards along straight sides from the plate's full width to the tab's 28 mm. The nut
  connection is Dev's as drawn, but the tab is 3 mm (his: 5): round end (r 10.5), Ø10.2 hole for the screw, the
  printed nut's flange against the tab's underside with its 4 pegs in the tab (16 mm square), body hanging below.
  Lowest plate position z 29.29 (a 35 mm pile under the lip); the nut is then at z 58.3…73.3, far above the coupling.
- **Front border (`border-b`, slides in):** in front of the lift plate, from the divider to the right wall, floor to
  the divider's top (z 96.04), filling the front strip to the box front. Its inner face is vertical 0.5 mm in front of
  the plate (y −135.9) up to the plate's lowest top (z 29.29), then leans forward at the slope's angle to y −142.0 at
  z 65, then vertical to the top, which carries the pusher's guide ridge. It keeps pile B from sliding out of the open
  front, and like the lift's slope it leads dropping cards into place.

## Case

- **One piece.** The gate gap (0.55 mm) is set by a chain: floor → frame → belt top, and floor → divider → the
  divider block's underside (the right gate). A joint anywhere in that chain adds its tolerance to the gap, and every
  split plane through the box crosses it or the walls that carry the motors. It fits the X2D: 193 × 217 mm on the plate,
  167 mm high (256 × 256 × 260; also the 235.5 mm width in dual-nozzle mode).
- **On its back** (back wall's outer face on the plate, the front edge up). Upright it can't be printed: the slot's roof
  (the divider block's underside) is a 103 mm bridge 1 mm above the divider; it would sag shut, and support can't come
  out of a 1 mm slit. On its back, walls, divider, slot, gate and lip are all extruded along the print direction: the
  slot and the gate gap lie in the XY plane (the printer's most precise, as the left gate), and nothing overhangs
  more than 45°. No supports.
- **What that changed** (all in "Changes to Dev's box"): faces towards the back wall would print in mid-air, so the
  right gate's bar runs into the back wall, pile H's back border is solid back to the wall, the pusher motor's hold has
  a 45° back, and the 4023's hold stops at the wall's outer face (the motor's last 5 mm overhang it). Holes across the
  print direction are teardrops pointing to the front edge (up).
- **Front borders as slide-in parts** (`border-h`, `border-b`): their card faces point at the back wall, so on its back
  each would be a 68 mm roof with nothing under it. Supporting them would need 90–130 mm tall trees through the piles'
  space, H's standing on its back border's card face. Printed separately on their ends, the card face is a plain wall. Each fills the
  front strip in front of its pile, flush with the box front, and has a stop: border H's step against the right gate's
  front face (y −141.4), border B's lip over the floor's front edge. 0.2 mm play per side; 2 printed pins each hold them
  ("Printed joints").
- **Walls 3 mm** (Dev's are 5), thinned from the outside so every inner face stays where it was; the floor too (its
  underside at z 2). The divider is 3 (Dev, x 72…75), cut back at its right face: the slot a card runs through is 2 mm
  shorter, and the left face (gates, lip, roller) stays put; everything right of it (right wall, lift, both motors, pusher, border B) moves 2 mm
  left with it, so pile B's compartment and the lift plate keep Dev's 67 mm. No pads: the pusher screw turns in the 3 mm left
  wall (Ø8.6), the pusher motor sits on the 3 mm right wall (M3×6). The pusher's notch goes through the right wall, so
  a 2 mm skin covers it from outside (z 94.74…134.54, front edge to y −36.4, back end at 45°); the motor's underside
  rests on its top. With the top cut (below) the solid volume drops 732 → 455 cm³.
- **Solid-looking blocks** (pile H's back border, the 4023's hold, the border inserts) are not printed solid: the
  slicer fills them with the infill (15 %). Hollowing them in CAD would replace that infill with walls of their own
  and save next to nothing (pile H's back border: ~23 g as a block, ~23 g as a card wall + top + 4 ribs). The lever
  is the infill: 10 % for the case changes nothing that carries load.
- **Top cut at z 178.84** (Dev's box went to 200): the top of the pusher motor's wall extension, the highest thing the
  case carries. The pusher reaches z 167.7, the lift's tab at push height z 144.
- **Feet:** two rails under the floor along the left and right edges, 42 mm (the lift motor hangs 40 below the floor),
  front and back open for the cables and the lift motor, which reaches 6.25 mm past the back wall. The roller frame's
  left part now hangs 40 mm above the table instead of standing on it: it's held by its step against the floor's left
  edge and the 4 floor screws (the loads are a few newtons).

## Files

All dimensions are in `params.scad`, in `dispenser.stl`'s own coordinates. Run `parts/cad build dispenser` for the
STLs and `parts/cad render dispenser` for the sheets (`parts/CLAUDE.md`).

| File | What | Print |
|---|---|---|
| `params.scad` | Box measurements, card, belts, drums, gears, layout | |
| `lib.scad` | Geometry of every piece, placed in the box frame | |
| `assembly.scad` | Preview in the box (cut box, lift, turned 4023, card, pile B); `part=` exports one placed piece; poses `-D push=0…1 -D lift_top=97.04` | – |
| `roller-frame.scad` | Back plate (holds the 4023), bed, 3 ribs (3 mm, a Ø6 boss at each front end for the front plate's screw) | bed down (upside down) |
| `front-plate.scad` | Front plate: 3 pegs into the rib ends, front pins pressed in, access holes for the motor screws | outer face down |
| `drive-drum.scad` | 16T gear + 3 grooves, 2 bearing seats, solid between the pin pockets | standing, gear down |
| `idler-drum.scad` | 3 grooves, 2 bearing seats, solid between the pin pockets | standing |
| `motor-gear.scad` | 20T, D-bore for the 4023's shaft, M3 set screw + nut slot | gear down |
| `gate-left.scad` | Left gate (the right one is part of the case) | on its side |
| `case.scad` | The case: Dev's box with every change and the feet | on its back, no supports |
| `border-h.scad`, `border-b.scad` | Front borders of piles H and B, slide in from the front | on their end (as in the file) |
| `pusher.scad` | Pusher carriage: plate, bar, nut tab | as placed, bottom down |
| `lift.scad` | Lift: plate, slope, Dev's nut tab (replaces Dev's lift) | as placed, plate down |
| `lift-support.scad` | Breakaway grid under the lift's tab, in the lift's coordinates: only in plate 3 | with the lift |
| `pusher-nut.scad` | TR8x8 nut (4 starts) of the pusher: brass nut's flange, body Ø11, 2 pegs into the tab | flange down |
| `lift-nut.scad` | TR8x8 nut of the lift: body Ø11, 45° cone up to the flange, 4 pegs into the tab | body down |
| `border-pin.scad` | Pin with a head, 4×: holds the borders through the case's screw holes | head down |
| `fit-test.scad` | Fit test for the printed joints: peg holes (vertical, horizontal), steel pin holes | as in the file |
| `plates/` | Print jobs: `cad.toml` → `[plates]`, built by `parts/cad plate dispenser` ("Print") | as in the file |
| `make_copy.py` | `ref/` (box, lift, 4023 from `dispenser.stl`) + `dispenser-roller.stl` (case, roller, pusher, lift, lift motor under the floor) | – |
| `check_pusher.py` | Collisions over the pusher's stroke with the lift at push height and a full pile, and of the lift with its nut (lowest, 2 between, push height) with the box, the pusher at home and its own motor and coupling | – |

```bash
cd parts/tools && uv run python ../dispenser/make_copy.py    # after any change, also regenerates ref/
cd parts/tools && uv run python ../dispenser/check_pusher.py # after a pusher, lift or box change: must print 0 overlapping
```

## Parts

| Qty | Part | Note |
|---|---|---|
| 3 | NBR O-ring 80 × 3 mm (ID × cord), 70 Shore A | The belts. ⚠️ Different size: change `oring_id`, the drum distance follows |
| 4 | 685ZZ bearing, 5 × 11 × 5 | Pressed into the drum ends (#59; MR105 5 × 10 × 4 fits too with `bearing` changed back) |
| 4 | Dowel pin Ø5 × 20 mm, ISO 8734 (hardened, m6), RS PRO | One per bearing, bought ready-made: nothing to cut (#57). Fixed: the drums turn on their bearings |
| 3 | M3×6 socket head | 4023 to the back plate (the 4th hole would cut into the drive pin's hole) |
| 1 | M3×5 set screw + M3 nut | Motor gear onto the shaft's flat (5 mm from the flat to the hub surface: flush) |
| 4 | M3×12 | Frame and front plate to the box floor, from below (2 each): they set the belt height, so screws, not pegs |

**Pusher and lift motor**

| Qty | Part | Note |
|---|---|---|
| 2 | 17HS4401 + Purecrea flexible coupling 8/5 + TR8x8 lead screw 200 mm | The two sets on the shopping list: pusher and lift. Screws uncut, nuts printed (`pusher-nut.scad`, `lift-nut.scad`) |
| 4 | M3×6 socket head | Through the 3 mm right wall into the pusher motor's face, from inside (M3×8 would bottom out) |
| 2 | M3×6 socket head | Through the 3 mm floor into the lift motor's face, from inside (M3×8 would bottom out) |

**Both screws are 200 mm, not cut** (stock length). Each goes into its coupling up to the motor shaft's tip. The
pusher's then sticks out of the left wall by 75 mm (x −78…−3, at y −145.9, z 155.69), the lift's 47 mm above the box
top (z 178.8…226, at x 109.8, y −16.9). ⚠️ Both turn while the dispenser runs: keep cables, hair and fingers away from
the ends; nothing may be mounted there.

Screws into printed parts go into 2.5 mm pilot holes (self-tapping): only the 4 frame screws are left (#63).

## Printed joints

Instead of screws wherever a joint only locates or carries a light load (#63). Every peg is Ø3 (`peg_d`) with a
chamfered tip; the holes take their fit from the fit test (below). Screws stay where something must be clamped: the
motors, the motor gear's set screw, and the roller's 4 floor screws (the belt height and with it the 0.55 gap depend on
the frame being pulled flat onto the floor; the left wall and the gate's legs leave it 0.3 mm to lift).

| Joint | How | Fit |
|---|---|---|
| Lift nut → lift tab | 4 pegs on the flange's top into the tab (Ø3 holes at ±8). The screw pushes the nut up into the tab, the lift's weight keeps it there; the pegs only stop it turning | `fit_press` (vertical) |
| Pusher nut → pusher tab | 2 pegs on the flange's tab side (above and below the screw). Pushing presses the flange onto the tab; the pressed pegs carry the light return stroke. No clip: a groove for one would cut into the nut's 1.3 mm wall under the thread | `fit_press_h` (horizontal, flat-topped teardrops) |
| Front plate → rib ends | 3 pegs, 5 long, into sockets in the rib bosses. They locate the plate; its 2 floor screws hold it | `fit_slip_h` (horizontal in the frame) |
| Front pins | Pushed through the front plate into the bearings by hand, flush with the outer face; the press fit holds them (no keepers). They come off with the plate | `pin_press_d` |
| Borders H, B | 4 printed pins (`border-pin`): head outside, through the case's Ø3.4 screw holes into the border. The case is unchanged | H: `fit_press` (vertical), B: `fit_press_h` |

**Fit test first** (`plates/0-fit-test.stl`, PETG 0.2 mm, ~15 min). Holes 1…6 count from the chamfered corner:

| Hole | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|
| Peg holes, gap per side (`fit_steps`) | −0.05 | 0 | +0.05 | +0.10 | +0.15 | +0.20 |
| Steel pin holes, Ø (`pin_steps`) | 4.85 | 4.90 | 4.95 | 5.00 | 5.05 | 5.10 |

Try one of the two printed pins in every hole of the 3 mm base (vertical holes) and of the standing wall (horizontal):
- **press** = the tightest hole the pin goes into fully by thumb, and stays in when you turn the part over;
- **slip** = the tightest hole it slides into and out of freely without wobbling.

Same with a Ø5 dowel pin in the 6 mm block: the tightest hole it goes into by firm hand pressure. Tell Claude the
five hole numbers (press and slip, vertical and horizontal; pin): they go into `params.scad`, plates 2 and 3 are
rebuilt. Until then the values are guesses ⚠️. The pegs themselves don't change, so plate 1 can print right away.

## Print (Bambu X2D)

**Print from `plates/`**: one STL per print job, parts already in print orientation, laid out and spaced, centred.
Drag one file into Bambu Studio, pick the filament and the settings in its row, slice, print. `stl/` holds the single
parts (for reprints, e.g. only the left gate). `parts/cad plate dispenser` rebuilds the plates from `cad.toml`.

| Plate | Parts | Filament | Settings (rest: Bambu's preset) |
|---|---|---|---|
| `0-fit-test.stl` | fit test, 2 test pins: **print first** ("Printed joints") | PETG | 0.20 mm, 4 walls, 15 %, no supports |
| `1-petg-fine.stl` | drive drum, idler drum, motor gear, left gate, pusher nut, lift nut, 4 border pins | PETG | 0.12 mm, 4 walls, 15 %, no supports, **brim: outer only** (the drums stand 107 mm on Ø15–18) |
| `2-petg.stl` | roller frame, front plate, border H, border B: **after the fit test** | PETG | 0.20 mm, 4 walls, 15 %, no supports |
| `3-pla.stl` | pusher, lift (+ its support): **after the fit test** | PLA | 0.20 mm, 4 walls, 15 %, **no supports**: the lift's is in the file |
| `stl/case.stl` | case (a plate of its own) | PETG | 0.20 mm, 4 walls, 15 %, no supports, textured PEI |

Why these settings:

- **Plate 1 at 0.12 mm:** fits and teeth (bearing seats, gear mesh, the nut's thread, the left gate's gap step). The
  nut needs 0.12 most: the 1.3 mm under its thread root is all perimeters. One layer height for the whole plate (a
  plate prints as one object); the drums only cost time at 0.12, not accuracy.
- **Brim on plate 1:** the file is one object, so Bambu's "auto" brim judges the whole plate and leaves the drums
  without one. Outer brim, 5 mm, Bambu's 0.1 mm gap: it peels off the gear teeth.
- **4 walls everywhere:** every wall is ≤ 3 mm, so 3 or 4 walls both print it solid; one value for all plates.
- **No supports:** horizontal holes are teardrops, the drum grooves are teardrop-shaped and the lower bearing seat
  ends in a 45° cone. The pusher's U and the lift's tab are bridges.
- **Lift's support** (`lift-support.scad`, only in plate 3): a 0.84 mm grid (6 mm cells, 0.6 mm pad on the bed) under
  the nut tab, 0.25 mm below it and 0.6 mm behind the slope, so it touches nothing: the tab's first layer sags onto
  it. Pull it out sideways after printing; clean the tab's underside (the nut flange's seat). Slice at 0.20 mm: the
  0.25 gap then always leaves one empty layer. At 0.28 it could fuse.
- **Card thickness:** the case is built for `card_t` 0.5 (sleeved, pressed flat; loose, bow and air make it > 1 mm)
  and `gap` 0.55. The belts sit at `66.04 − gap`, the right gate's lip a card + 0.2 below them, the slot's bottom just
  under the lip. If the gap needs changing, the printed case stays and only the frame moves the belts ("Tuning the
  gap"); the legs leave 0.5 above the plates for that.
- **Case:** on its back as the STL comes, the long side (217) along the plate's Y. ~0.5 kg PETG by my estimate (493 cm³
  solid). Textured PEI, no brim: the whole back wall is on the plate. Check the 4023 window's edges after the first
  layers (elephant foot: keep Bambu's compensation on, the window has only 0.3 mm play). Why one piece and on its
  back: "Case" above.
- **Borders** stand on their ends; border H is 72 mm tall on a 25 × 22 mm foot. Add a brim only if it wobbles.
- The left gate prints on its side so the gap step lies in the XY plane, where the printer is most accurate.
- **Lift and pusher: PLA.** Neither carries more than a few newtons. PLA is stiffer than PETG and holds the sliding
  edges and the lift's 0.44 / 0.56 mm side gaps more accurately. The limit is heat: PLA softens from ~55 °C, and at
  home the pusher's nut sits 1.5 mm from the coupling and its plate's top corner ~2 mm under the motor (through the
  wall's 2 mm skin). Run both motors at moderate current with a low hold current (a 17HS4401 at its full 1.7 A gets
  hotter than that) ⚠️.
- Lift: plate down. Every other side down either stands the 3 mm plate on its edge or turns the slope's sides past 45°.
- Pusher carriage: as it sits in the box, bottom down: the plate's lower edge, the one that slides, comes out of the
  bed flat, and the chamfers take the elephant foot. The nut hole is a teardrop with its tip cut flat (a full tip
  would cut into the upper peg hole). Every other side down puts the tab or the plate in mid-air.
- **Lift nut: body down.** Its pegs face the tab, which is above the flange, so they have to print upward: the flange
  sits on a 45° cone. It stands on a 26 mm² ring, 18 mm tall: plate 1's brim holds it.
- **Fit test (plate 0) at 0.20 like plates 2 and 3**, whose holes it tunes. The lift and pusher are PLA: their holes
  get the PETG values, PLA prints holes a little larger ⚠️.
- **TR8 nuts: PETG**, not PLA: it wears less on the steel screw. Test each on the screw by hand before assembly: it
  should turn along the whole screw without binding and without wobble. Binds → `nut_slop` + 0.05, wobbles → − 0.05
  (per side, `params.scad`) ⚠️ untested. A printed nut drags more than brass: set the pusher's StallGuard threshold
  with these nuts.

## Assembly

The 4023 sits in the back wall and the right gate (case) is above the frame's right end, so the frame goes in first,
from the left, and everything else follows from behind and from the front.

0. **Borders** (any time): border B over the floor from the front until its lip stops at the floor's front edge,
   2 border pins from below through the floor's holes, pressed in by thumb. Border H from the front between the left
   wall and the right gate until its step stops at the gate's front face, 2 border pins through the left wall from
   outside. To take a border out, pull its pins by the heads (pliers).

1. **Bearings:** press 2 685ZZ into each drum, pushing only on the outer race.
2. **Frame into the box:** left gate pressed into its sockets first. Slide the frame in from the open left side, under
   the left wall and the right gate's legs, until its step sits against the left edge of the box floor. 2× M3×12 from
   below into the back plate.
3. **Motor:** cable plugged in. Push the 4023 through the back wall's window from behind, sliding on the hold,
   connector down in the channel, cable out under the box. Boss into the plate's recess, 3× M3×6 from the front;
   the heads sink into the plate. The drum gear will run over them.
4. **Motor gear:** on the shaft, flush with the drum gear's plane (7 mm in front of the motor face). Tighten the set screw on the flat.
5. **Drums:** drop the 2 back pins into the back plate's blind holes, then slide the drums onto them from the front with
   the drive drum's gear at the back, meshing. The drive drum passes under the right gate's lip and front leg.
6. **Belts:** slide the 3 O-rings over both drums from the front, into the grooves. They pass over the bed and the ribs.
7. **Front plate:** its 3 pegs into the rib ends, 2× M3×12 from below through the floor. Then push the 2 front pins
   through its holes into the front bearings until flush with the plate's outer face (firm hand pressure: the plate
   holds them by press fit; never hammer through a bearing). For new O-rings: floor screws out, pull the plate off,
   the pins come with it.

**Lift:** motor under the floor (boss into the floor's hole, 2× M3×6 from inside), coupling on the shaft inside the
box (z 13.5…38.5), tighten the 5 mm side. Nut under the lift's tab, flange up, its 4 pegs pressed into the tab's holes, body
down. Outside the box, turn the
screw through the nut (the lift can't turn once it's in). Lower lift and screw in together, screw end into the
coupling, tighten the 8 mm side.

**Pusher:**

1. **Motor:** coupling on the shaft (its middle at the shaft's tip, 24 mm from the face), tighten the 5 mm side.
   Motor onto the right wall's outer face, boss in the wall's hole, 4× M3×6 from inside the box.
2. **Nut:** body through the tab from the right (motor side), its 2 pegs pressed into the tab's holes above and below.
3. **Carriage in:** borders in, lift at push height (turn its screw by hand). The ridge is in the way of sliding the
   carriage straight into the notch, so put it in halfway along instead (plate over the lift, around x 100): from the
   front, held ~3 mm high, then set down with the groove over the ridge and the plate on the lift. Hold it there.
4. **Screw in:** through the left wall's hole from outside, thread it through the nut by turning it, into the coupling.
   Tighten the 8 mm side through the open front. Turn the screw to bring the carriage home into the notch.
5. **Check:** turn the screw by hand with the lift at push height. The plate must slide on the lift, step down onto the
   divider, and come back over the lift's edge and into the notch without catching.

## Tuning the gap

1. `gap` = 0.55 (set by Dev), `card_t` = 0.5 the sleeved card pressed flat (upper bound). Keep the case as printed:
   a different gap only changes the frame, front plate and left gate.
2. **Take** (left gate): if 2 cards pass or the bottom card jams, reprint only the left gate, e.g.
   `parts/cad build dispenser gate-left -D gate_gap=0.6`, and swap it.
3. **Feed** (right gate): its gap = the divider block's underside (z 66.04) − the belt top. Changing it means new belts
   heights: `parts/cad build dispenser roller-frame front-plate gate-left -D gap=0.5` (never the case: it stays at
   0.55, its legs leave room to raise the belts by 0.4), or shims under the frame to raise them.

## Changes to Dev's box (`case.scad`)

`case.scad` makes all of these from `ref/box.stl` (Dev's box as drawn in `dispenser.stl`), so the case prints without
any work in Onshape. Draw them there only to keep Onshape in step; and if `dispenser.stl` is ever re-exported with
them, `case_add()` / `box_cuts()` have to stop adding them, or they're added twice.

- **Box 10.6 mm shorter:** front edge at y −154.4 instead of −165 (floor, side walls, divider).
- **Lift:** Dev's is dropped; the printed one from `lift.scad` replaces it.
- **Right gate** (the divider's upper block extended to the left):
  - bar x 67.5…72, y −141.4 to the back wall, underside at z 66.04 (= the block's underside), inner face vertical up
    to z 69.6, then the wedge: a straight face from (x 67.5, z 69.6) to (x 71.8, z 95.54), meeting the divider's top;
  - legs x 67.5…72 over the frame plates, y −141.4…−135.6 and −26.5…−20.5, from z 63.09 up into the bar (the front one 0.2 short of
    the plate's back face: the funnel's edge would otherwise run exactly through its corner);
  - lip x 68.67…72 under the card path (0.5 into the divider), y −136.4…−25.5 (into both legs), top at z 64.79, underside a circle r 9.6 around
    the drive drum's axis (x 62, z 57.09) down to its 45° point, then a 45° face down to the divider; the circle also
    cuts the front leg, so the drive drum slides through;
  - slit ends between lip and bar outside the funnel (45° lines below).
- **Pile H's back border:** x −1…72 (from inside the left wall to the divider), z 70.5…96.04, inner face from
  y −39.9 at the bottom leaning back to y −35.55 at the top, solid back to the back wall.
- **Front borders: separate parts** (`border-h`, `border-b`), each filling the front strip to the box front:
  - H: x 0.2…71.8, z 70.5…96.04, inner face from y −131.9 at the bottom leaning forward to y −136.25 at the top;
    beside the right gate (y > −141.6) it stops 0.2 short of the gate's bar and wedge. 2 pin holes from the left
    face at (y, z) = (−146, 78), (−146, 89).
  - B: x 75.2…142.8, z 5…96.04, inner face at y −135.9 up to z 29.29, then leaning forward to y −142.0 at z 65,
    vertical above; a lip
    2 mm thick down the floor's front edge (z 2.5…10). 2 pin holes from below at (x, y) = (93, −146), (125, −146).
- **4023 in the back wall:** window through the wall at x 28.55…71.45, z 22.39…64.99 (the motor + 0.3, its floor
  flush with the motor's underside). Hold under the motor: a block x 28.85…71.15, y −20.2…−2 (from 0.3 behind the
  frame's back plate to the wall's outer face; the motor's last 5 mm stick out behind it), z 2…22.39. Cable channel
  x 40…60, y −6.6…−2 (open at the back), from the hold's top down through the floor.
- **Holes Ø3.4 in the box floor:** frame at (x, y) = (20, −23.5), (60, −23.5), (20, −138.4), (60, −138.4); border B at
  (93, −146), (125, −146). **Through the left wall:** 2× Ø3.4 at (y, z) = (−146, 78), (−146, 89) for border H.
- Keep free for the camera: the bed has a window at x 10…46, y −61.9…−43.9 (between the back edge of the card and the first belt).
- **Divider 3 mm** (x 72…75): Dev's 5 cut back at the right face; the box right of it (floor, back wall, right
  wall) moved 2 mm left to meet it, the box 2 mm narrower.
- **Divider slot 1.31 mm tall:** the divider's lower part cut down from z 65 to 64.73 inside the funnel below.
- **Divider slot filled at both ends** (z 64.73…66.04, x 72…75), leaving the card's width open. Each fill ends in a 45°
  face toward the card. Front: from the front edge (y −154.4) to the line (72, −135.4)→(75, −132.4). Back: from
  the back wall (y −5) to the line (72, −36.4)→(75, −39.4). Seen from above, the opening is 99 mm wide at x 72 and 93 mm
  at x 75. This also ties the divider's upper block to the lower part.
- **Pusher guide ridge** (see "How the pusher works") on the divider's top, x 72.1…74.9 (its front face set back
  0.6 at both ends over 1.2 mm), and on both borders' tops, x 0.2…71.8 and 75.2…142.8 (set back over 2 mm).
- **Pusher notch** through the 3 mm right wall, open to the front edge, a 2 mm skin outside it (x 146…148, z
  94.74…134.54, to y −36.4, back end 45°): y −154.4…−40.4 at z 96.74…107.54 (bar), plus y −136.4…−40.4 up to z
  132.54 (plate).
- **Right wall extended forward** around the pusher motor: x 143…146, to y −169.05, z 133.04…178.84, the underside
  at 45° back to the wall's front edge.
- **Pusher motor hold:** a wedge on the right wall's outer face under the motor: y −153.9…−137.9, top at z 134.54 (the
  motor's underside, level with the notch skin's top) from x 146 to 186, 3 mm thick there, underside at 45° down to
  the wall at z 90.54; its back face
  runs at 45° back along the wall to y −96.9.
- **Holes through the right wall** for the pusher motor: Ø22.6 for the boss at (y, z) = (−145.9, 155.69), 4× Ø3.4 at
  (−161.4, 140.19), (−130.4, 140.19), (−161.4, 171.19), (−130.4, 171.19).
- **Screw hole Ø8.6 through the left wall** at (y, z) = (−145.9, 155.69).
- **Lift motor under the floor:** in the floor at (x, y) = (109.8, −16.9) a hole Ø22.6 for the boss and 2× Ø3.4 at
  (94.3, −32.4), (125.3, −32.4) (the back 2 would be under the wall). The motor reaches 6.25 mm past the back wall.
- **Box top cut at z 178.84** (the pusher motor's wall extension's top).
- **Walls and floor 3 mm** (outer faces at x −3, x 146, y −2, z 2), no pads ("Case").
- **Feet:** 2 rails under the floor, 3 mm, x 0…3 and 143…146, z −40…2, back wall to front edge.
- Horizontal holes (in the case's print direction) are teardrops pointing to the front edge.
- The spare 17HS4401 + screw + nut at x 354…396 are now the pusher's, and the lift's motor and screw move under the
  floor: the copy drops them and places its own.
- The pusher motor sticks out 40 mm to the right (x 186) and 13 mm in front of the box (y −167.05), the wall extension 15 mm.

## ⚠️ Open: feed stops short

A fed card is driven only while it sits on the belt. Its trailing edge stops over the drive drum (x 62), so the card
ends at x 62…128.5, while the divider ends at x 75. The last 13 mm still sit in the slot and under the right gate, on
the lip. The lip keeps them a card + 0.2 mm below the belt top, so the next card slides over them instead of into their edge,
but then two cards sit in the slot. No roller left of the divider can push further. Options:

1. **Software, test first:** feed fast so the card slides on over pile B until it hits the right wall (x 143). It then
   ends at x 76.5…143: clear of the divider by only 1.5 mm with the sleeved card (66.5 long), squared against the
   wall. That needs ≳ 0.31 m/s at the belt (~5.7 drum turns/s, ~340 rpm at the motor; card on card μ ≈ 0.3, 16.5 mm
   to slide); a bounce off the wall of more than 1.5 mm leaves it on the divider. ⚠️ A pancake stepper should reach that
   without load. Test whether it does with the card.
2. **Hardware:** a driven pinch roller on the lift side behind the slot, geared from the drum through the gear zone
   behind the cards (y −27.5…−34.5, 5.9 mm behind them). It would have to get out of the way when the lift pushes B back over to the left.

## ⚠️ Untested assumptions

- `card_t` = 0.5 mm is Dev's upper bound for a sleeved card pressed flat (< 0.5); loose it's > 1 mm (bow, air in the
  sleeve). The gates rely on the pile and the bar flattening it. Exact value: "Tuning the gap".
- `pile_max` = 35 mm for 30 cards assumes they settle to ~1.2 mm each in a pile. Sleeved cards loose are > 1 mm each:
  stack 30 sleeved cards loosely and measure the height. If it's over 35, the pusher plate, the lift's lowest
  position and the pusher's screw height change (all from `pile_max`).
- Pile H's back edge is 5.9 mm in front of the drum gear, whose tips reach 0.6 mm above the belts (y −34.5…−27.5,
  x ~59…65). Nothing positions H in Y: a card placed more than 5.9 mm toward the back rubs on the turning gear.
- The 685ZZ press fit (`press`; only 1.7 mm wall at the idler's ends, ~1.2 mm under the drive drum's gear roots: if the gear ring cracks or the mesh binds after pressing, lower `press`), the 5.2 mm pin holes and the printed gear mesh (`gear_slack`, `gear_backlash`) need a test print.
- Printed joints (#63): every hole fit is a guess until the fit test is in `params.scad`. The lift nut's pegs only
  stop it turning: if the lift ever sticks while going down, the nut leaves the tab, and once the pegs are out
  (2.7 mm) it turns with the screw and the lift stays up. The lift home switch would then never trigger: treat a
  homing timeout as "lift stuck". The front pins' press fit has to hold against the bearing's drag only.
- Pins instead of a through rod (#57): the printed drum carries the belt tension (~15 N) between its bearings itself.
  Estimate ~0.3 mm sideways bend toward the other drum, more as PETG creeps; belt height and gate gap don't change, the
  belts lose a little of their ~18 mm stretch. If they go slack: a through rod Ø5 × 119.9 needs only the drum's bore back.
- NBR on the card back is assumed to grip better than card on card. If the second card comes along, lower the gap first.
- The 4023's model (connector side, shaft flat at 2.0 mm from the axis) is taken from `dispenser.stl` as drawn.
- Pusher: the plate rests on the lift by its weight (~5 mNm holding torque, estimated for 21 g printed; tips at ~2.5 N
  push, then the guide ridge caps the rise at ~0.4 mm: check that a card can't slip under at a jam). The lift's push height has to be calibrated once: push a single card lying at the front (y ≈ −129.4), then one
  at the back (y ≈ −46.4), and change the height in 0.05 mm steps until both are caught. Bowed cards may still slip
  under the plate's front end; if they do, the fix is a comb (slots in the lift plate) so the plate's
  edge can dip below the lift top.
- Pusher: the screw turns in a plain Ø8.6 hole in the left wall (PETG on steel, slow, light load). Coupling (Ø19 × 25)
  and nut dimensions (TR8 nut Ø22 flange on a Ø16 bolt circle) are taken from Dev's model and the usual sizes.
