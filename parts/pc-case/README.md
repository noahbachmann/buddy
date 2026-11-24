# PC case (HP all-in-one board)

A compact PETG case for the board, CPU cooler and metal tray taken out of an HP all-in-one (#87). The tray stays the
bottom of the board part. One flat top over everything (43.8 mm high) so a later module (SSD, power/USB boards,
Wi-Fi/BT) can screw on anywhere over the board. Footprint 353 × 262 mm: the board part is 250 × 227, and the cooler part
sticks out 103 mm to the right and 35 mm at the back.

## Parts

| Part | What | Print | PETG (solid, max) |
|---|---|---|---|
| `fit-check` | Flat template of the cooler corner: cooler-top outline, cooler outline + 1 mm, board's right and back edges | `plates/0-fit-check` | 5 g |
| `cooler-top` | Lattice skin, slotted intake over the fan hub, exhaust slits over the heatsink's outlet face, walls where it is outside, tabs to the lid | `plates/1-cooler-top`, skin down | 80 g |
| `cooler-bottom` | Floor under the cooler next to the tray (lattice), 3 nut tabs for cooler-top | `plates/2-cooler-bottom` | 30 g |
| `lid` | Lattice over the board (front 60 mm over the metal shield: open), 18 top-module mounts, cable notches, windows, seam tabs | `plates/3-lid`, skin down, 250 mm wide: alone on the plate | 128 g |
| `base` | Ribs under the tray (2.4 mm) and 6 nut tabs the lid screws to (the tray has no holes) | `plates/4-base` | 33 g |

All: PETG, 0.20 mm, 3 walls, 15 %, no supports (every part checked: no overhang > 45°, no bridge, no floating layer).
Walls and skins 2.4 mm. Mass is the solid upper bound; with 15 % infill only the 2.4 mm walls stay solid.

**Print `fit-check` first.** Lay it on the cooler with its two straight reference lines on the board's right and back
edges: the cooler must sit inside the inner ring, and the fan's curve must follow the arc. Then the cooler parts, then
lid and base.

## Hardware

| Joint | Screws | Nuts |
|---|---|---|
| lid ↔ base (front and back walls, from outside) | 6 × M3 × 8 | 6, slid into the base tabs from above |
| lid ↔ cooler-top (seam tabs, inside) | 4 × M3 × 8, head on the cooler side | 4, slid into the lid tabs from below |
| cooler-top ↔ cooler-bottom (left and front walls, from outside) | 3 × M3 × 8 | 3, into the cooler-bottom tabs from above |
| top module → lid | per module | up to 18, pressed into the hex pockets under the lid's mounts |

## Assembly

1. Nuts into the base tabs, cooler-bottom tabs and the lid's seam tabs (and the top-module pockets you'll use).
2. Base under the tray (tabs at the tray's front and back edges, ribs under it).
3. Cooler-bottom under the cooler, its floor next to the tray.
4. On the bench: lid and cooler-top skin down, screw them together through the 4 seam tabs (screw from the cooler
   side, into the lid's nuts).
5. Set lid + cooler-top down as one piece. Cables through the notches: left wall (ribbon), right wall (ribbons), back
   wall (antenna wires).
6. Screw the lid to the base (6 screws, front and back) and cooler-top to cooler-bottom (3 screws).

## Top-module mounts

18 M3 holes through the lid's top, a nut pocket under each, on the lattice nodes (world frame: x from the tray's left
edge, y from its front edge): x 20/60/100/140 × y 60/100/140/180, plus (180, 60) and (220, 60). Top face at z 43.8.
A module screws down through its own base into these nuts: screw length = module base + 3.9 (skin + boss above the
nut) + 2.4 (nut).

## Measurements and what isn't measured

Measured (Dev, 2026-11-24): tray 250 × 210 × 28; cooler 162 × 128 (heatsink) / 115 (fan) × 38; cooler 40 past the
tray's back edge, 100 past its right edge; fan housing 200 from its left edge (the 12 mm left of it: bracket strip).

⚠️ Scaled from the photo (±4 mm), all in `params.scad`:

- Fan-side arc of the cooler outline (`seam_pts`), fan hub (`fan_c`), heatsink outlet span (`fins_y`): fit-check shows them.
- Cable notches (`cable_left`, `cable_right`, `cable_back`).
- Height of the tray's side edges (`rail_h` 5 mm): the side walls of lid and cooler-top stop 0.5 above it. If the
  edges are taller, the lid won't go down: measure it before printing the lid.
- The tray is assumed to stop where the fan begins (the cooler floor only covers what is outside the tray). The base's
  right rib stops at y 108 so it stays out from under the fan either way.
- The tray's underside is assumed flat (the base ribs touch it along three lines).

## Design notes

- One flat top: the cooler is the tallest part (38), so the board part gets ~13 mm of air above it. Dev chose "make
  the cooler or tray taller so it connects" over a step.
- Lid and cooler-top split along two straight seams 9 mm off the cooler (room for a tab pair and a screw head). The
  inside stays open across the seam: the heat pipe and fan cable cross it, and the blower also pulls air through the
  lid's lattice over the board.
- No feet under the cooler floor: a raised floor would print as a bridge. The fan breathes through the top grille.
- Every joint is the same: a tab with a horizontal M3 hole and a nut slot open at its free end, so the nut drops in
  from above in print orientation (no bridge), and the screw comes from outside. Holes are teardrops pointing up in
  print. Clearance 3.4 mm (screws pass; tolerances stack over 250 mm), nut slots 5.7 × 2.7 (slip), top-module
  pockets 5.6 across flats (press: the nuts stay in when the lid is turned over).
- `lid` and `cooler-top` are built as one solid each (outline extruded, inside hollowed, tabs left standing): separate
  walls and skin left coplanar faces and the STLs weren't watertight.
