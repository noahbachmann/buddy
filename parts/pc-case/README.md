# PC case (HP all-in-one board)

A compact PETG case for the board, CPU cooler and metal tray taken out of an HP all-in-one (#87). The tray, with its
own 28 mm side walls and the silver plate the board sits on (8 mm under the board), is the bottom and the sides.
The lid fits between the tray walls, top flush with them at 28. The cooler gets a two-part cover, 40 mm high at most
(Dev): cover-bottom, a 1.6 mm floor under the heatsink (which sits 13 mm above the table) and in front of it, with the
fan standing on the table in an opening; cover-top over it (cooler 38 + 0.4 gap + 1.6 top = 40). The silver plate
stays: it continues behind the board, under the cover's left and back walls.

## Parts

| Part | What | Print | PETG (solid, max) |
|---|---|---|---|
| `fit-check` | Flat template of the cooler corner: cover outline, cooler outline + 1 mm, tray's right and back edges | `plates/0-fit-check` | 5 g |
| `cover-top` | Over the cooler, standing on cover-bottom: lattice top (1.6), slotted intake over the fan hub, exhaust slits over the heatsink's outlet face (from 14 mm up), windows. Its walls facing the tray stand on the lid and carry 4 screw bosses; over the tray wall and the silver plate its walls stop above them | `plates/1-cover-top`, top down | 72 g |
| `cover-bottom` | Lattice floor (1.6) under the raised heatsink and in front of it, a strip behind the fan under the back wall; 5 low nut tabs (≤ 11.6, under the heatsink) for cover-top | `plates/3-cover-bottom`, floor down | 17 g |
| `lid` | Lattice plate between the tray walls (front 60 mm over the metal shield: open), 4 tabs into the slits beside the board that carry it, 18 top-module nut bosses underneath, 3 cable notches | `plates/2-lid`, top down, 250 mm wide: alone on the plate | 62 g |

All: PETG, 0.20 mm, 3 walls, 15 %, no supports (no overhang > 45°, no bridge, no floating layer). Plates 2.4 mm.

## The lid in the tray

- Edges 0.3 mm inside the tray walls' inner faces: 249.4 wide, 100 from its right edge to the heatsink's end (95 from
  the tray wall's outer face).
- In the ~3 mm slits between the board's edges and the tray walls: 2.0 mm tabs (0.5 play each side), standing on the
  silver plate 8 mm under the board. They carry the lid. Left y 68–103, right y 62–100 (y from the front edge).
- Cable notches: left edge y 54–66 (ribbon out through the left slit), right edge y 104–128 (ribbons out over the
  right tray wall; the cover's front wall is open there up to 32 mm), back edge x 160–176 (antenna wires).

**Before the real parts:** `skeleton/` has fit-test skeletons of lid, cover-top and cover-bottom (84 g together,
same geometry where it matters): assemble them on the hardware first (`skeleton/README.md`).

## Hardware

| Joint | Screws | Nuts |
|---|---|---|
| lid → cover-top (from under the lid, up into the cover's seam-wall bosses) | 4 × M3 × 10 | 4, slid into the bosses from inside the cover |
| cover-top → cover-bottom (through the front, right and back walls, from outside) | 5 × M3 × 8 | 5, dropped into the floor tabs from above |
| top module → lid | per module | up to 18, pressed into the hex pockets under the lid's nodes |

## Assembly

1. Nuts into the cover-top's 4 bosses (from inside), the 5 cover-bottom tabs (from above) and the top-module pockets
   you'll use (from below the lid).
2. Cover-bottom under the cooler: the fan stands in its opening, the floor runs under the heatsink.
3. On the bench: cover-top top down, lid top down next to it; screw up through the lid into the cover's bosses.
4. Lower the pair: lid tabs into the slits beside the board, cover-top over the cooler onto cover-bottom. Cables
   through the notches. Screw cover-top to cover-bottom (5, from outside).

The lid isn't fixed to the tray: it stands on its tabs, located by them and by the cover around the cooler. Lift both
together to open the case.

## Top-module mounts

18 M3 holes through the lid's lattice nodes, a nut pressed into a 2 mm boss under each. World frame (x from the tray
wall's inner face, y from the front edge): x 20/60/100/140 × y 60/100/140/180, plus (180, 60) and (220, 60). Lid top
at z 28. Screw length ≈ module base + 5 (1.9 mm of lid above the nut, nut 2.4, ~1 spare).

## Measurements and what isn't measured

Measured (Dev, 2026-11-24): tray 250 between its side walls, walls 28, 3 mm slits, board 8 mm above the silver plate;
cooler 162 × 128 (heatsink) / 115 (fan) × 38, the fan on the table, the heatsink ~13 mm above it (ruler photo); case ≤ 40 high; heatsink end 100 from the right wall's inner face, 95 from
its outer face; cooler 40 past the tray's back edge; fan housing 200 from its left edge (the 12 mm left of it: bracket
strip).

⚠️ Not measured, all in `params.scad`:

- Fan housing's corner (`seam_pts`): read off the fit-check photo; the cover doesn't depend on it (≥ 8 mm clear).
- Slit spans (`slit_tabs_left/right`) and cable notches: from photos.
- Silver plate top under the board (`plate_z` 1), its edge beside the fan (`plate_x1` 200) and how high plate, foam
  pads and antenna wires reach there (`plate_clear` 14: the cover's walls stop above it).
- Where the tray's right wall ends towards the fan (the ghost assumes at the cooler, y 122).
- Heatsink underside (`hs_z0` 13, the floor tabs stay under it), fan ↔ heatsink boundary (`fan_x1` 297) and the fan's
  front edge (`fan_y0` 137): the floor's opening for the fan.

## Design notes

- Lid and cover split along two straight seams 9 mm off the cooler. The lid reaches 4 mm past the cover's seam walls,
  under their bosses. Inside, the cover is open towards the board: the heat pipe and fan cable cross freely, and the
  blower also pulls air through the lid's lattice.
- Both parts print top down: the lid's tabs and bosses point down in use, so nothing on the lid points up. That's why
  the screws come from under the lid into the cover, and the top-module nuts sit underneath.
- Cover bosses: full section, then 45° into the wall (what they grow out of in print); the nut pocket's floor in use
  is a 45° V, its roof in print. Clearance holes 3.4 mm, nut pockets 5.7 × 2.7 (slip), top-module pockets 5.6 across
  flats (press).
- `cover-top` is one solid (outline extruded, inside hollowed, bosses added, then cut away over the tray and the plate):
  separate walls and top left coplanar faces and the STL wasn't watertight.
