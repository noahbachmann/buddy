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
- **Why two parts:** a closed O-ring can't go around anything that is held at both ends. The frame (front plate, bed,
  ribs) and the back plate (4023, motor gear, back pins) are separate (Dev, 2026-11-24: the back plate is where all the
  screwing is, easier with no bed over it). The O-rings go on the drums while those sit on the back pins; the frame then
  slides in from the front with the bed between the belt runs (a 1.5 mm chamfer on its back edge lifts the belts).
  Ribs 0 and 1 end in 3 mm tongues, full height up to the bed's top (they print from the build plate up), that go into
  pockets open at the back plate's top: they line the frame up with it, so the front and back pins stay coaxial. The
  tongues' tips are chamfered like the bed's edge.
- **Into the box:** the right gate's legs and lip are in the way from above, so the back plate slides in from the open
  left side (under the left wall and the right gate's legs), the drums onto its pins from the front, then the frame from
  the front. The bed is open over both drums and the drum gear along its whole length, and the left gate's front peg runs
  in a slot through the bed into the front plate's socket. Swept in 1 mm steps against the case, border B, back plate,
  left gate, drums, motor gear and back pins: only the foot sliding on the floor touches.

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
  wide, 45° underside) carries it, so it doesn't hang on its screws alone. The printed plug coupling (Ø16 × 25, `plug-coupling.scad`)
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
  pile B: front and back (left wall to the divider, z 65.09…96: from 0.4 under the belt top, so no card slides out
  under them to the front or back) with inner faces 0.5 mm off the cards at the bottom, leaning out ~10° by 5.3 mm
  to the top (the back one solid to the back wall, the front one the slide-in `border-h`); on the divider side, a wedge on the right gate from its
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
  front face (y −141.4), border B's lip over the floor's front edge. 0.2 mm play per side. They carry the pusher's
  guide ridge, so each is held at **both ends** (Dev, 2026-11-26): at the outer end by printed pins through the case
  (H: 2 through the left wall; B: 3 through the right wall + 2 up through the floor), at the divider end by **2 ties**:
  Ø5 × 20 steel dowel pins pressed through the divider (Ø5.05, as the roller's pin holes) at (y, z) = (−148, 73) and
  (−148, 87), 8.5 mm out on each side. A border can't be pushed onto a pin along X once it's in, so each has a slot
  per tie open to its front face: it slides in from the front over the ties (slot Ø5.2, 0.3 past the pin, so the
  borders' own stops still locate them). The ties hold them in Z and against the ridge's push to the back.
- **Walls 3 mm** (Dev's are 5), thinned from the outside so every inner face stays where it was; the floor too (its
  underside at z 2). The divider is 3 (Dev, x 72…75), cut back at its right face: the slot a card runs through is 2 mm
  shorter, and the left face (gates, lip, roller) stays put; everything right of it (right wall, lift, both motors, pusher, border B) moves 2 mm
  left with it, so pile B's compartment and the lift plate keep Dev's 67 mm. No pads: the pusher screw turns in the 3 mm left
  wall (Ø8.6), the pusher motor sits on the 3 mm right wall (M3×6). The pusher's notch goes through the right wall, so
  a 2 mm skin covers it from outside (z 94.74…134.54, front edge to y −36.4, back end at 45°); the motor's underside
  rests on its top. With the top cut (below) the solid volume drops 732 → 466 cm³.
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

### Case spares (`case-spares.scad`)

The same case with ~88 cm³ less solid volume (465.5 → 377.3 cm³, ≈ 112 g PETG: everything removed is ≤ 3 mm wall,
which prints solid). Same print: on its back, no supports. `case.scad` stays as it was; print either one.

- **Rear top cut down:** behind the pusher motor the walls end at z 135.54 (3 over the pusher's notch; the highest card
  is at 132.04, the pushed pile on the lift at push height). In front of y −121.75 (motor face + 3) full height for
  the motor and the screw, 45° in between (prints as a 45° edge).
- **Back wall windows** (on the bed, any shape): above pile H's back border (x 6…69, z 102.04…129.54), and behind
  pile B: open from the floor to a crossbar at z 64.29…70.29, above it a solid post behind the lift tab's path
  (x 96.3…123.3; the tab runs 1.4 mm off the wall, the screw can lean it that far, and it
  must rub on a wall, not catch a window's edge), the slots beside the post X-braced (3 mm struts). The wall under pile H (4023, cable, camera) stays closed:
  no stray light under the bed.
- **Floor under the lift:** a window pointed to the front (its roof in print), flat at the back 3 mm short of the
  lift motor's face. The lift plate never comes lower than z 26.
- **Feet:** Warren truss, 3 triangular windows per rail, 45° diagonals, 6.5 mm bottom chord.
- Not cut: walls and divider wherever a card, the lift or the pusher runs along them; the floor under the roller
  (frame screws, camera mount to come); the solid-looking blocks (they print at infill anyway, see above).
- Stiffness: the back wall still ties both side walls up to z 135.54 and is closed under pile H; the rims are ≥ 6 mm.

## Files

All dimensions are in `params.scad`, in `dispenser.stl`'s own coordinates. Run `parts/cad build dispenser` for the
STLs and `parts/cad render dispenser` for the sheets (`parts/CLAUDE.md`).

| File | What | Print |
|---|---|---|
| `params.scad` | Box measurements, card, belts, drums, gears, layout | |
| `lib.scad` | Geometry of every piece, placed in the box frame | |
| `assembly.scad` | Preview in the box (cut box, lift, turned 4023, card, pile B); `part=` exports one placed piece; poses `-D push=0…1 -D lift_top=97.04` | – |
| `roller-frame.scad` | Frame: front plate (front pins, access holes for the motor screws), bed, 3 ribs (3 mm; ribs 0 and 1 with tongues into the back plate), slot for the left gate's front peg | bed down (upside down) |
| `back-plate.scad` | Back plate: holds the 4023, back pins (blind), pockets for the rib tongues | top edge down (upside down), brim |
| `drive-drum.scad` | 16T gear + 3 grooves, 2 bearing seats, solid between the pin pockets | standing, gear down |
| `idler-drum.scad` | 3 grooves, 2 bearing seats, solid between the pin pockets | standing |
| `motor-gear.scad` | 20T, D-bore for the 4023's shaft, M3 set screw + nut slot | gear down |
| `gate-left.scad` | Left gate (the right one is part of the case) | on its side |
| `case.scad` | The case: Dev's box with every change and the feet | on its back, no supports |
| `case-spares.scad` | The case with less filament ("Case spares") | on its back, no supports |
| `border-h.scad`, `border-b.scad` | Front borders of piles H and B, slide in from the front | on their end (as in the file) |
| `pusher.scad` | Pusher carriage: plate, bar, nut tab | as placed, bottom down |
| `lift.scad` | Lift: plate, slope, Dev's nut tab (replaces Dev's lift) | as placed, plate down, auto supports under the tab |
| `pusher-nut.scad` | TR8x8 nut (4 starts) of the pusher: brass nut's flange, body Ø11, 2 pegs into the tab | flange down |
| `plug-coupling.scad` | Rigid coupling without bolts: D-bore pressed onto the 5 mm shaft, TR8x8 thread the screw turns into (used) | motor end down |
| `coupling.scad` | Older clamp coupling (round bores, 2× M3×12 across a slot): kept as the fallback | motor end down |
| `lift-nut.scad` | TR8x8 nut of the lift: body Ø11, 45° cone up to the flange, 4 pegs into the tab | body down |
| `border-pin.scad` | Pin with a head, 4×: holds the borders through the case's screw holes | head down |
| `fit-test.scad` | Fit test for the printed joints: peg holes (vertical, horizontal), steel pin holes | as in the file |
| `pilot-test.scad` | Pilot test: M3 pilot holes 2.6…3.0 in a 6 mm wall, as in the roller plates' feet | as in the file |
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
| 6 | Dowel pin Ø5 × 20 mm, ISO 8734 (hardened, m6), RS PRO | One per bearing, bought ready-made: nothing to cut (#57). Fixed: the drums turn on their bearings. 2 more: the ties through the divider that hold both front borders |
| 3 | M3×6 socket head | 4023 to the back plate (the 4th hole would cut into the drive pin's hole) |
| 1 | M3×5 set screw + M3 nut | Motor gear onto the shaft's flat (5 mm from the flat to the hub surface: flush) |
| 4 | M3×12 | Back plate and frame to the box floor, from below (2 each): they set the belt height, so screws, not pegs. Plain machine screws, threaded into pilot holes sized by the pilot test |

**Pusher and lift motor**

| Qty | Part | Note |
|---|---|---|
| 2 | 17HS4401 + TR8x8 lead screw 200 mm | Pusher and lift. Screws uncut; nuts and couplings printed (`pusher-nut.scad`, `lift-nut.scad`, `plug-coupling.scad`). The Purecrea flexible couplings from the shopping list aren't used (below) |
| 4 | M3×6 socket head | Through the 3 mm right wall into the pusher motor's face, from inside (M3×8 would bottom out) |
| 2 | M3×6 socket head | Through the 3 mm floor into the lift motor's face, from inside (M3×8 would bottom out) |

**Both screws are 200 mm, not cut** (stock length). Each goes into its coupling up to the motor shaft's tip. The
pusher's then sticks out of the left wall by 75 mm (x −78…−3, at y −145.9, z 155.69), the lift's 47 mm above the box
top (z 178.8…226, at x 109.8, y −16.9). ⚠️ Both turn while the dispenser runs: keep cables, hair and fingers away from
the ends; nothing may be mounted there.

Screws into printed parts go into pilot holes (`screw_pilot`, from the pilot test): only the 4 frame screws are left (#63).

## Printed joints

Instead of screws wherever a joint only locates or carries a light load (#63). Every peg is Ø3 (`peg_d`) with a
chamfered tip; the holes take their fit from the fit test (below). Screws stay where something must be clamped: the
motors, the motor gear's set screw, and the roller's 4 floor screws (the belt height and with it the 0.55 gap depend on
the frame being pulled flat onto the floor; the left wall and the gate's legs leave it 0.3 mm to lift).

| Joint | How | Fit |
|---|---|---|
| Lift nut → lift tab | 4 pegs on the flange's top into the tab (Ø3 holes at ±8). The screw pushes the nut up into the tab, the lift's weight keeps it there; the pegs only stop it turning | `fit_press` (vertical) |
| Pusher nut → pusher tab | 2 pegs on the flange's tab side (above and below the screw). Pushing presses the flange onto the tab; the pressed pegs carry the light return stroke. No clip: a groove for one would cut into the nut's 1.3 mm wall under the thread | `fit_press_h` (horizontal, flat-topped teardrops) |
| Frame → back plate | Ribs 0 and 1: 3 mm full-height tongues into pockets open at the back plate's top. They line the frame up; its 2 floor screws hold it | `tongue_fit` 0.1 per side ⚠️ untested |
| Steel pins | Back ones in the back plate's blind holes, front ones pushed through the front plate into the bearings, flush with its outer face. All 4 holes Ø5.05 (Dev measured 2026-11-24: Ø5.2 printed ~5.2, Ø4.95 vertical printed 4.9) | `pin_hole` |
| Borders H, B | 7 printed pins (`border-pin`): head outside, through the case's Ø3.4 holes into the border | H, B through the right wall: `fit_press` (vertical); B from below: `bpin_up_fit` (−0.025, tighter than `fit_press_h`: the pins hang and fall out at 0) |
| Ties → divider, borders | 2 steel Ø5 × 20 pins pressed through the divider, centred (8.5 out each side); the borders slide over them in slots open to the front | divider `pin_hole` (horizontal teardrop), slots `pin` + 2 × `fit_slip` |

**11 holes, 7 pins:** the case has 11 Ø3.4 holes along the front and under the roller. 7 take border pins (2 through
the left wall into border H, 3 through the right wall and 2 up through the floor into border B). The other 4 (floor, under the roller) take the frame's
4× M3×12: they press their own thread into the pilot holes in the back plate's and the frame's feet, so a pin doesn't
fit there by design. Border B's pins hold against gravity: a pin that slides out of B means its hole is
too big, not a pin that's too short.

**Fit test first** (`plates/0-fit-test.stl`, PETG 0.2 mm, 10 % infill, ~15 min). Holes 1…6 count from the chamfered corner:

| Hole | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|
| Peg holes, gap per side (`fit_steps`) | −0.05 | 0 | +0.05 | +0.10 | +0.15 | +0.20 |
| Steel pin holes, Ø (`pin_steps`) | 4.85 | 4.90 | 4.95 | 5.00 | 5.05 | 5.10 |

Try one of the two printed pins in every hole of the 3 mm base (vertical holes) and of the standing wall (horizontal):
- **press** = the tightest hole the pin goes into fully by thumb, and stays in when you turn the part over;
- **slip** = the tightest hole it slides into and out of freely without wobbling.

Same with a Ø5 dowel pin in the 6 mm block: the tightest hole it goes into by firm hand pressure. Tell Claude the
five hole numbers (press and slip, vertical and horizontal; pin): they go into `params.scad`, plates 2 and 3 are
rebuilt. The pegs themselves don't change, so plate 1 can print right away.

Result (2026-11-09): vertical press 3, slip 4; horizontal press 2, slip 3 (vertical holes print smaller). The steel
pin: Dev measured the first roller print instead (2026-11-24, above): `pin_hole` Ø5.05 in both plates.

Checked on the borders (2026-11-11): printed from a stale plate 2 with the pre-test fits, border H's Ø3.0 vertical
holes (= hole 2) were too tight and border B's Ø3.1 horizontal holes (= hole 3) let the pin slide out: both as the fit
test predicts, also in 7 mm deep holes. Current: H Ø3.1, B Ø3.0.

**Thread test** (printed 2026-11-17 on the 1b reprint plate). The first gear and nuts (2026-11-16) didn't fit: the gear's
D-bore was Ø5.15 (between press and slip: it predates the fit test), and the nuts had 0.2 mm radial gap, which on a
TR thread's 15° flanks is only 0.05 mm per flank (the flanks carry, not the crests), below even the press fit. Now
`bore_gap` = slip (Ø5.2, flat at 2.1) with a 0.4 chamfer at the bed face against elephant foot, and `nut_flank` is
the gap per flank. Ribs on a ring = its number:

| Ring | 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| Nut rings: gap per flank (`nut_flank_steps`) | 0.10 | 0.15 | 0.20 | 0.25 |
| D-bore rings: gap per side (`bore_steps`) | 0.10 | 0.15 | 0.20 | – |

Nut: the tightest ring that turns along the screw by fingers without binding, ring flat in the fingers (no rocking on
the screw). Bore: the tightest ring that slides onto the motor shaft by hand. The gear and nuts on the same plate have the
middle values (nut ring 2, bore ring 1): if they fit, done. If a ring other than that one is the right one, tell Claude
its number: it goes into `nut_flank` / `bore_gap`, and that part is reprinted from `stl/`.

**Result (Dev, 2026-11-17):** nut ring 1 fits (0.10 per flank), so `nut_flank` = 0.10. Bore ring 1 (0.10) is too tight,
ring 2 (0.15) very slightly loose, so `bore_gap` = 0.125 ⚠️ (between two tested steps). The gear and nuts from 1b
(bore 0.10, nuts 0.15) are replaced by `plates/1c-reprint.stl`.

**Pilot test** (`plates/0c-pilot-test.stl`, PETG 0.2 mm, ~10 min). The first roller plates had Ø2.5 pilots: an M3
machine screw (core Ø2.39) can't be turned into them by hand (Dev, 2026-11-25). Vertical holes print ~0.1 small, so
that hole was the screw's core: the thread had to push aside its full depth of PETG, and a machine screw has no
cutting edge. 2.5 is the pilot for screws made for plastic (PT/K30 type), not for M3 machine screws. The test block
is the plates' feet: a 6 mm wall, holes 10 deep, open at the top with a 0.5 mm lead-in, holes 1…5 from the chamfered
corner:

| Hole | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| Pilot Ø (`pilot_steps`) | 2.6 | 2.7 | 2.8 | 2.9 | 3.0 |

Turn an M3×12 into each hole with an Allen key or a screwdriver, all the way, then tighten it gently. Pick the
**tightest hole it goes into without a fight** that still grips: the screw doesn't spin freely when tightened and
doesn't pull out by hand. Tell Claude the number: it goes into `screw_pilot` and `plates/2b-roller-reprint.stl`
is rebuilt and reprinted. Already printed plates: a drill bit of that size turned by hand (fingers or a pin vise)
opens the holes in soft PETG, no machine needed.

**Result (Dev, 2026-11-25):** hole 2 (Ø2.7) is right for M3, so `screw_pilot` = 2.7; plate 2b is rebuilt with it.

**Couplings: printed and rigid** (2026-11-17). The Purecrea flexible couplings let the screws flop: a flexible
coupling only works when the screw has its own bearing at the far end, and the lift's screw has none (its top is
free). A rigid coupling lets the motor's bearings hold the screw straight. No bought rigid coupling fit, so it's
printed: Ø22 × 25 PETG, round bores (5 and 8, `cpl_gap` 0.15 per side) meeting at a step in the middle (the screw
stops on it), one slot along the whole length, and per end one M3×12 across the slot that clamps the bore shut
(the clamp centres the shaft along its whole length, a grub screw would tilt it). `check_pusher.py`: no collisions at Ø22.

**Plug coupling** (2026-11-19, replaces the clamp coupling): Ø16 × 25 PETG, no bolts. Lower half a D-bore pressed
onto the shaft (`plug_bore` 0.10 per side: the D-bore press fit from the thread test), upper half a TR8x8 internal
thread (`plug_flank` 0.10, as the nuts) the screw turns into until its end jams on the step in the middle (12.5 mm
each). The shaft and screw ends sit where they did with the clamp coupling; `check_pusher.py`: no collisions.
⚠️ Assumes the 17HS4401's shaft has a D-flat (4.5 mm across, as the 4023's) along its last 12.5 mm: check before
printing; a round shaft needs the clamp coupling. ⚠️ The jam is the only thing that keeps the screw from turning
out: it holds while the screw's torque stays below the tightening torque. Torque in the screw's unscrewing
direction (the motor reversing against a load or a stall) is what can loosen it. Tighten firmly; if a screw works loose
in use, the fix is a cross pin through coupling and screw, not tighter threads.

## Print (Bambu X2D)

**Print from `plates/`**: one STL per print job, parts already in print orientation, laid out and spaced, centred.
Drag one file into Bambu Studio, pick the filament and the settings in its row, slice, print. `stl/` holds the single
parts (for reprints, e.g. only the left gate). `parts/cad plate dispenser` rebuilds the plates from `cad.toml`.

| Plate | Parts | Filament | Settings (rest: Bambu's preset) |
|---|---|---|---|
| `0-fit-test.stl` | fit test, 2 test pins: **print first** ("Printed joints") | PETG | 0.20 mm, 4 walls, 10 %, no supports |
| `0c-pilot-test.stl` | M3 pilot holes for the roller's floor screws ("Pilot test"): before plate 2b | PETG | 0.20 mm, 4 walls, 15 %, no supports |
| `1-petg-fine.stl` | drive drum, idler drum, motor gear, left gate, pusher nut, lift nut, 2 couplings, 4 border pins | PETG | 0.12 mm, 4 walls, 15 %, no supports, **brim: outer only** (the drums stand 107 mm on Ø15–18) |
| `1d-plug-couplings.stl` | 2 plug couplings | PETG | 0.12 mm, 4 walls, **100 % infill**, no supports |
| `1c-reprint.stl` | motor gear, pusher nut, lift nut with the thread test's values ("Thread test") | PETG | 0.12 mm, 4 walls, 15 %, no supports, **brim: outer only** (the lift nut) |
| `2-petg.stl` | roller frame, back plate, border H, border B: **after the fit test** | PETG | 0.20 mm, 4 walls, 15 %, no supports, **brim** (the back plate stands on its 6 mm edge) |
| `2b-roller-reprint.stl` | roller frame, back plate: the split roller (2026-11-24) with Ø5.05 pin holes, Ø2.7 M3 pilots | PETG | as plate 2 |
| `2c-borders.stl` | border H, border B with the tie slots and B's right-wall holes, 3 more border pins (2026-11-26) | PETG | as plate 2 (no brim needed) |
| `3-pla.stl` | pusher, lift: **after the fit test** | PLA | 0.20 mm, 4 walls, 15 %, **supports on: tree (auto), on build plate only** |
| `stl/case.stl` | case (a plate of its own) | PETG | 0.20 mm, 4 walls, 15 %, no supports, textured PEI |
| `stl/case-spares.stl` | or this one instead: the case with ~112 g less PETG | PETG | as the case |

Why these settings:

- **Plate 1 at 0.12 mm:** fits and teeth (bearing seats, gear mesh, the nut's thread, the left gate's gap step). The
  nut needs 0.12 most: the 1.3 mm under its thread root is all perimeters. One layer height for the whole plate (a
  plate prints as one object); the drums only cost time at 0.12, not accuracy.
- **Brim on plates 1 and 1c:** the file is one object, so Bambu's "auto" brim judges the whole plate and leaves the drums
  and the lift nut without one. The lift nut stands on its Ø11 body (~30 mm², 16 mm² in the first layer: the thread's
  lead-in) with the Ø22 flange 15 mm up: without a brim it came loose on the 1b reprint (2026-11-17, spaghetti). Outer brim, 5 mm,
  Bambu's 0.1 mm gap: it peels off the gear teeth.
- **4 walls everywhere:** every wall is ≤ 3 mm, so 3 or 4 walls both print it solid; one value for all plates.
- **No supports:** horizontal holes are teardrops, the drum grooves are teardrop-shaped and the lower bearing seat
  ends in a 45° cone. The pusher's U is a bridge.
- **Supports on plate 3:** only the lift's nut tab needs them; Bambu Studio generates them ("on build plate only"
  keeps them off the lift's own faces). Clean the tab's underside after removing them (the nut flange's seat).
- **Card thickness:** the case is built for `card_t` 0.5 (sleeved, pressed flat; loose, bow and air make it > 1 mm)
  and `gap` 0.55. The belts sit at `66.04 − gap`, the right gate's lip a card + 0.2 below them, the slot's bottom just
  under the lip. If the gap needs changing, the printed case stays and only the frame moves the belts ("Tuning the
  gap"); the legs leave 0.5 above the plates for that.
- **Case:** on its back as the STL comes, the long side (217) along the plate's Y. ~0.5 kg PETG by my estimate (466 cm³
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
  should turn along the whole screw without binding and without wobble. Binds → `nut_flank` + 0.05, wobbles → − 0.05
  (per flank, `params.scad`; from the thread test: 0.10). A printed nut drags more than brass: set the pusher's StallGuard threshold
  with these nuts.

## Assembly

The 4023 sits in the back wall and the right gate (case) is above the plates' right ends, so the back plate goes in
first, from the left, and everything else follows from behind and from the front.

0. **Ties, then border B** (any time): press the 2 steel pins into the divider's Ø5 holes (front strip, z 73 and 87)
   until 8.5 mm stick out on each side (measure; a clamp or a vise with soft jaws, never a hammer on the case). Border
   B over the floor from the front, its slots over the ties, until its lip stops at the floor's front edge; 2 border
   pins from below through the floor's holes and 3 through the right wall, pressed in by thumb. To take a border out, pull its pins by the heads
   (pliers).

1. **Bearings:** press 2 685ZZ into each drum, pushing only on the outer race.
2. **Back plate into the box:** left gate pressed into the back plate's socket first. Slide the plate in from the open
   left side, under the left wall and the right gate's legs, until its step sits against the left edge of the box
   floor. 2× M3×12 from below.
3. **Motor:** cable plugged in. Push the 4023 through the back wall's window from behind, sliding on the hold,
   connector down in the channel, cable out under the box. Boss into the plate's recess, 3× M3×6 from the front;
   the heads sink into the plate. The drum gear will run over them. Nothing is in front of the plate yet.
4. **Motor gear:** on the shaft, flush with the drum gear's plane (7 mm in front of the motor face). Tighten the set screw on the flat.
5. **Drums:** drop the 2 back pins into the back plate's blind holes, then slide the drums onto them from the front with
   the drive drum's gear at the back, meshing. The drive drum passes under the right gate's lip and front leg.
6. **Belts:** the 3 O-rings over both drums, into the grooves.
7. **Frame:** from the front, bed between the belts' top and bottom runs (its chamfered back edge lifts them), the left
   gate's front peg in the bed's slot, until the rib tongues sit in the back plate's pockets and the gate's peg in the
   front plate's socket. 2× M3×12 from below through the floor. Then push the 2 front pins through the plate into the
   front bearings until flush with its outer face (firm hand pressure; never hammer through a bearing). For new
   O-rings: border H out (step 8), front pins and floor screws out, pull the frame off to the front.
8. **Border H**, last: it reaches down to under the belt top, so the drum gear and the O-rings can't pass under it.
   From the front between the left wall and the right gate, its slots over the ties, until its step stops at the
   gate's front face, 2 border pins through the left wall from outside.

**Coupling** (`plug-coupling.scad`, both screws): D-bore half onto the motor shaft, flat to flat, pressed on until
the shaft's tip reaches the step in the middle (shaft end on a hard surface, push the coupling by hand or with a
vise; never hammer the motor's shaft: its bearings take it). The screw then turns into the thread half until its end
jams on the step/shaft tip; tighten firmly by hand (hold the coupling with pliers wrapped in a cloth). A coupling
that turns on the shaft by hand is too loose: reprint it with a smaller `plug_bore`.

**Lift:** motor under the floor (boss into the floor's hole, 2× M3×6 from inside), coupling pressed on the shaft
(z 13.5…38.5). Nut under the lift's tab, flange up, its 4 pegs pressed into the tab's holes, body
down. Outside the box, turn the
screw through the nut (the lift can't turn once it's in). Lower lift and screw in together, screw end into the
coupling's thread, turn it in until it jams, tighten.

**Pusher:**

1. **Motor:** coupling pressed on the shaft (its middle at the shaft's tip, 24 mm from the face).
   Motor onto the right wall's outer face, boss in the wall's hole, 4× M3×6 from inside the box, **loosely** for now.
2. **Nut:** body through the tab from the right (motor side), its 2 pegs pressed into the tab's holes above and below.
3. **Carriage in:** borders in, lift at push height (turn its screw by hand). The ridge is in the way of sliding the
   carriage straight into the notch, so put it in halfway along instead (plate over the lift, around x 100): from the
   front, held ~3 mm high, then set down with the groove over the ridge and the plate on the lift. Hold it there.
4. **Screw in:** through the left wall's hole from outside, thread it through the nut by turning it, into the coupling's
   thread until it jams; tighten (hold the coupling through the open front). Now the motor's 4 screws: the coupling is rigid and the screw also
   turns in the left wall's hole, so the motor has to line up with that hole. Turn the screw by hand while
   tightening them one by one; if it gets stiff, loosen, nudge the motor (its boss has 0.3 mm play), retry. Then turn
   the screw to bring the carriage home into the notch.
5. **Check:** turn the screw by hand with the lift at push height. The plate must slide on the lift, step down onto the
   divider, and come back over the lift's edge and into the notch without catching.

## Tuning the gap

1. `gap` = 0.55 (set by Dev), `card_t` = 0.5 the sleeved card pressed flat (upper bound). Keep the case as printed:
   a different gap only changes the frame, back plate and left gate.
2. **Take** (left gate): if 2 cards pass or the bottom card jams, reprint only the left gate, e.g.
   `parts/cad build dispenser gate-left -D gate_gap=0.6`, and swap it.
3. **Feed** (right gate): its gap = the divider block's underside (z 66.04) − the belt top. Changing it means new belts
   heights: `parts/cad build dispenser roller-frame back-plate gate-left -D gap=0.5` (never the case: it stays at
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
- **Pile H's back border:** x −1…72 (from inside the left wall to the divider), z 65.09…96.04, inner face from
  y −39.9 at the bottom leaning back to y −34.63 at the top, solid back to the back wall. Below the left wall's
  edge (z 70.5) only x 0.2…67.5: the left gate and the funnel's mouth at the right gate stay free. A channel r 9.6
  around the drive drum's axis (y −40.9…−25.5) for its gear, which slides in under it from the front. Its underside
  clears the bed by 2.5, the drum's rim by 0.76 and the 4023 by 0.4 (the window's top is 64.99).
- **Front borders: separate parts** (`border-h`, `border-b`), each filling the front strip to the box front:
  - H: x 0.2…71.8, z 65.09…96.04, inner face from y −131.9 at the bottom leaning forward to y −137.17 at the top;
    beside the right gate (y > −141.6) it stops 0.2 short of the gate's bar and wedge. 2 pin holes from the left
    face at (y, z) = (−146, 78), (−146, 89); 2 tie slots from its divider end (8.8 deep, open to the front).
  - B: x 75.2…142.8, z 5…96.04, inner face at y −135.9 up to z 29.29, then leaning forward to y −142.0 at z 65,
    vertical above; a lip
    2 mm thick down the floor's front edge (z 2.5…10). 2 pin holes from below at (x, y) = (93, −146), (125, −146),
    3 from the right face at (y, z) = (−148, 20), (−148, 50), (−148, 80); 2 tie slots from its divider end.
- **4023 in the back wall:** window through the wall at x 28.55…71.45, z 22.39…64.99 (the motor + 0.3, its floor
  flush with the motor's underside). Hold under the motor: a block x 28.85…71.15, y −20.2…−2 (from 0.3 behind the
  frame's back plate to the wall's outer face; the motor's last 5 mm stick out behind it), z 2…22.39. Cable channel
  x 40…60, y −6.6…−2 (open at the back), from the hold's top down through the floor.
- **Holes Ø3.4 in the box floor:** frame at (x, y) = (20, −23.5), (60, −23.5), (20, −138.4), (60, −138.4); border B at
  (93, −146), (125, −146). **Through the left wall:** 2× Ø3.4 at (y, z) = (−146, 78), (−146, 89) for border H.
  **Through the right wall:** 3× Ø3.4 at (y, z) = (−148, 20), (−148, 50), (−148, 80) for border B. **Through the
  divider:** 2× Ø5.05 at (y, z) = (−148, 73), (−148, 87) for the ties.
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
- Pusher: the screw turns in a plain Ø8.6 hole in the left wall (PETG on steel, slow, light load). Nut dimensions (TR8 nut Ø22 flange on a Ø16 bolt circle) are taken from Dev's model and the usual sizes.
