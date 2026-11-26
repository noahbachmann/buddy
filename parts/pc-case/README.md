# PC case (HP all-in-one board)

A compact PETG case for the board, CPU cooler and metal tray taken out of an HP all-in-one (#87). The tray, with its
own 28 mm side walls and the silver plate the board sits on (8 mm under the board), is the bottom and the sides.
The lid fits between the tray walls, its top at 34 (Dev 2026-11-25: nearly the cover's height), 6 above them. The cooler gets a two-part cover, ~40 mm high
(Dev: ≤ 40; 40.4 for a floor under the fan): cover-bottom, a 1.6 mm lattice floor under the heatsink (13 mm above the
fan's base) and in front of it, and a solid 0.8 mm floor under the fan, which stands on it; cover-top over it (0.8 floor
+ cooler 38 + 0.4 gap + 1.2 top = 40.4). The silver plate
stays: it continues behind the board, under the cover's left and back walls.

## Parts

| Part | What | Print | PETG (solid, max) |
|---|---|---|---|
| `fit-check` | Flat template of the cooler corner: cover outline, cooler outline + 1 mm, tray's right and back edges | `plates/0-fit-check` | 5 g |
| `hook-test` | One lid front-edge hook with 10 mm of the lid's front band each side, cut from the real lid: snap it over the tray's front edge to check reach, hook and play | `plates/0-hook-test`, top down | 4 g |
| `cover-top` | Over the cooler (4 mm off the heatsink on the right: felt), standing on cover-bottom: lattice top (1.2), slotted intake over the fan hub, exhaust slits over the heatsink's outlet face (from 14.8 mm up), windows. Its walls facing the tray stand on the lid and carry 4 screw bosses (lid to top, head recessed); over the tray wall and the silver plate its walls stop above them | `plates/1-cover-top`, top down | 64 g |
| `cover-bottom` | Lattice floor (1.6) under the raised heatsink and in front of it; solid 0.8 floor under the fan outside the tray (it lies flat on the table: no bending to carry) that the fan stands on; 5 low nut tabs (≤ 11.6, under the heatsink) for cover-top | `plates/3-cover-bottom`, floor down | 24 g |
| `lid` | Lattice plate between the tray walls (front 60 mm over the metal shield: open; ~14 mm windows behind it), 4 tabs into the slits beside the board that carry it, a 25 mm wall down its back edge, 14 top-module and 4 cover nut bosses underneath, 3 cable notches, thinner over the CPU | `plates/2-lid`, top down, 252 mm wide: alone on the plate | 93 g |

All: PETG, 0.20 mm, 3 walls, 15 %, no supports (no overhang > 45°, no bridge, no floating layer; the lid's two 3 mm
hook overhangs print without). Plates 2.4 mm.

## The lid in the tray

- Edges 0.3 mm inside the tray walls' inner faces: 252.0 wide (252.6 between the walls: the 250 lid skeleton was one
  tab width short, the 251.4 one 0.6, Dev 2026-11-25), ~97 from its right edge to the heatsink's end (95 from the tray
  wall's outer face).
- Top at 34, plate 31.6–34: 3.6 above the tray walls (28), so the walls no longer guide its edges; the slit tabs
  (now 31.6 tall) locate it. The gap between wall tops and plate stays open (air).
- Back edge (y 210), left side up to the cover: a 2.4 wall down to z 9 (25 from the top; Dev 2026-11-26: 3 deeper),
  notched for the antenna wires. ⚠️ Its bottom edge is level with the board's top face (plate 1 + 8, from the params):
  nothing on the board may stand under it there.
- Front edge (y 0, over the metal shield): 2 hooks, 20 wide at x 50 and 215: a straight 2.4 arm down 35 from the lid's top,
  its inner face flush with the lid's front edge (which sits 0.3 out past the tray plate's edge: play), a 3 × 3 hook pointing in under the silver plate (its top 32 under the
  lid's top: Dev measured the plate's underside there). The lid snaps on: the arm bends out over the plate's edge. So the
  lid stays on the tray by itself, cover or not. ⚠️ The tray must not stand flat on the table at its front edge (the
  hooks reach 1 below the plate). In print, each hook's catching face is a 3 × 20 overhang (Bambu prints that).
- In the ~3 mm slits between the board's edges and the tray walls: 2.0 mm tabs (0.5 play each side), standing on the
  silver plate 8 mm under the board. They carry the lid. Left y 68–103, right y 55.5–92 (y from the front edge; Dev 2026-11-26: 8 towards the front, 1.5
  shorter).
- Over the CPU block (x 38–165, y 106–197): plate 1.2 instead of 2.4, thinned from below, and no mount bosses: 1.2 more
  air under the bars, 3.2 under the nodes (Dev 2026-11-26). The top stays flat: it's the print bed face.
- Cable notches: left edge y 54–66 (ribbon out through the left slit), right edge y 104–128 (ribbons out over the
  right tray wall; the cover's front wall is open there up to 38 mm), back edge x 160–176 (antenna wires).

**Before the real parts:** `skeleton/` has fit-test skeletons of lid, cover-top and cover-bottom (89 g together,
same geometry where it matters): assemble them on the hardware first (`skeleton/README.md`).

## Hardware

| Joint | Screws | Nuts |
|---|---|---|
| cover-top → lid (from the cover's top, head recessed, down into the lid) | 4 × M3 × 8 (socket head) | 4, pressed into the bosses under the lid |
| cover-top → cover-bottom (through the front, right and back walls, from outside) | 5 × M3 × 8 | 5, dropped into the floor tabs from above |
| top module → lid | per module | up to 14, pressed into the hex pockets under the lid's nodes |

## Assembly

1. Nuts pressed into the lid's 4 cover bosses and the top-module pockets you'll use (from below the lid), dropped into
   the 5 cover-bottom tabs (from above).
2. Cover-bottom under the cooler: the fan stands on the thin floor, the lattice floor runs under the heatsink.
3. Lid in: tabs into the slits beside the board, cables through the notches.
4. Cover-top over the cooler onto cover-bottom and the lid. Screw it to the lid (4, from the top) and to cover-bottom
   (5, from outside).

The lid isn't fixed to the tray: it stands on its tabs, located by them and by the cover around the cooler. Undo the 4
top screws to lift it out alone.

## Top-module mounts

14 M3 holes through the lid's lattice nodes, a nut pressed into a 2 mm boss under each. World frame (x from the tray
wall's inner face, y from the front edge): x 20–220 (step 40) × y 60/100, plus x 20 × y 140/180.
None over the CPU (x 60–140 × y 140–180: removed for air, 2026-11-26). Lid top
at z 34, 6.4 under the cover's top. Screw length ≈ module base + 5 (1.9 mm of lid above the nut, nut 2.4, ~1 spare).

## Measurements and what isn't measured

Measured (Dev, 2026-11-24): tray 250 between its side walls (252.6 after two lid skeleton prints, 2026-11-25), walls 28, 3 mm slits, board 8 mm above the silver plate;
cooler 162 × 128 (heatsink) / 115 (fan) × 38, the fan on the table, the heatsink ~13 mm above it (ruler photo); case ≤ 40 high (40.4 accepted for the fan floor,
2026-11-25); heatsink end 100 from the right wall's inner face (98 with the 252), 95 from
its outer face; cooler 40 past the tray's back edge; fan housing 200 from its left edge (the 12 mm left of it: bracket
strip).

⚠️ Not measured, all in `params.scad`:

- Fan housing's corner (`seam_pts`): read off the fit-check photo; the cover doesn't depend on it (≥ 8 mm clear).
- Slit spans (`slit_tabs_left/right`) and cable notches: from photos.
- Silver plate top under the board (`plate_z` 1), its edge beside the fan (`plate_x1` 200) and how high plate, foam
  pads and antenna wires reach there (`plate_clear` 14: the cover's walls stop above it).
- Where the tray's right wall ends towards the fan (the ghost assumes at the cooler, y 122).
- Heatsink underside above the fan's base (`hs_dz` 13, the floor tabs stay under it), fan ↔ heatsink boundary
  (`fan_x1` 297) and the fan's front edge (`fan_y0` 137): where the floor steps from 1.6 lattice to 0.8 solid.

## Design notes

- Cover-top's right wall stands 4 mm off the heatsink (back wall 1): the heat pipe's end nearly touched it (Dev
  2026-11-26). The 3 mm is for self-adhesive felt or 2–3 mm cork on the wall's inside where the pipe is, only on the
  solid parts (front corner y 113–128, below the slits up to z 14.8): over the exhaust slits it would block the
  heatsink's outlet.
- Lid and cover split along two straight seams 9 mm off the cooler. The lid reaches 4 mm past the cover's seam walls,
  under their bosses. Inside, the cover is open towards the board: the heat pipe and fan cable cross freely, and the
  blower also pulls air through the lid's lattice.
- Both parts print top down: the lid's tabs, wall and bosses point down in use, so nothing on the lid points up. That's
  why every nut under the lid is pressed in from below.
- Lid ↔ cover screws: only 6.4 between the lid's top and the cover's, too little for a nut slid into the cover (V
  floor + nut + roof). So the screw comes from the top: square 9 mm boss on the seam wall from the lid to the top
  (its axis 1.5 outside the wall's centre so the lid's Ø11 nut boss clears the lid's edge), head recess Ø6.2 × 3.6, a
  45° cone down to the 3.4 hole (the head's seat; in print a roof at 45°, no bridge). M3 × 8: head seat at 36.45,
  through 2.4 of boss and the lid into the pressed nut, tip 1.2 out under the boss.
- Clearance holes 3.4 mm, slip-fit nut slots 5.7 × 2.7, press-fit pockets 5.6 across flats.
- The fan floor lifts the cooler 0.8 mm. The cooler also hangs on the CPU by its heat pipe: if it can't rise that much,
  the floor levers on it. Check with the skeleton (`skeleton/README.md`, step 3); `fan_floor_t` 0.6 is the fallback.
- `cover-top` is one solid (outline extruded, inside hollowed, bosses added, then cut away over the tray and the plate):
  separate walls and top left coplanar faces and the STL wasn't watertight.
