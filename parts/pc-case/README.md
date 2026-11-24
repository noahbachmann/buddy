# PC case (HP all-in-one board)

A compact PETG case for the board, CPU cooler and metal tray taken out of an HP all-in-one (#87). The tray, with its
own 28 mm walls, is the bottom and the sides of the board part. The lid lies on those walls, and a cover goes over the
cooler. Heights: lid top 30.4 mm, cover 41 mm (cooler 38 + 0.6 gap + 2.4 top). Footprint 353 × 253 mm.

## Parts

| Part | What | Print | PETG (solid, max) |
|---|---|---|---|
| `fit-check` | Flat template of the cooler corner: cover outline, cooler outline + 1 mm, tray's right and back edges | `plates/0-fit-check` | 5 g |
| `cover` | Over the cooler, standing on the table: lattice top, slotted intake over the fan hub, exhaust slits over the heatsink's outlet face, windows. Its two walls facing the tray (seam walls) stand on the lid | `plates/1-cover`, top down | 83 g |
| `lid` | Flat lattice plate on the tray walls (front 60 mm over the metal shield: open), 18 top-module mounts, 4 tabs for the cover, 3 cable notches | `plates/2-lid`, 250 mm wide: alone on the plate | 62 g |

All: PETG, 0.20 mm, 3 walls, 15 %, no supports (no overhang > 45°, no bridge, no floating layer). Plates 2.4 mm.

**Print `fit-check` first.** Lay it on the cooler with its two straight reference lines on the tray's right and back
edges: the cooler must sit inside the inner ring, and the fan's curve must follow the arc. Then cover, then lid.

## Hardware

| Joint | Screws | Nuts |
|---|---|---|
| cover → lid (through the seam walls into the lid's tabs) | 4 × M3 × 8, heads inside the cover | 4, dropped into the lid tabs from above |
| top module → lid | per module | up to 18, pressed into the hex pockets from under the lid |

## Assembly

1. Nuts into the 4 lid tabs (and the top-module pockets you'll use).
2. On the bench: cover top down, lid next to it, screw the cover's seam walls to the lid tabs (heads inside the cover).
3. Lay the pair on: the lid on the tray walls, the cover over the cooler on the table. Ribbon cables up through the
   notches in the lid's left and right edges, antenna wires through the one in the back edge.

The lid isn't fixed to the tray: it lies on the walls, held in place by the cover around the cooler (the cooler hangs
on the board through the heat pipe). Lift both together to open the case.

## Top-module mounts

18 posts (Ø11, 4 mm tall) on the lattice nodes, an M3 hole through each and a nut pressed in from below. World frame
(x from the tray's left edge, y from its front edge): x 20/60/100/140 × y 60/100/140/180, plus (180, 60) and (220, 60).
Post tops at z 34.4. A module screws down through its own base: screw length = module base + 4 (post above the nut) +
2.4 (nut) + 1.5.

## Measurements and what isn't measured

Measured (Dev, 2026-11-24): tray 250 × 210, walls 28; cooler 162 × 128 (heatsink) / 115 (fan) × 38, standing on the
table; cooler 40 past the tray's back edge, 100 past its right edge; fan housing 200 from its left edge (the 12 mm left
of it: bracket strip). The case should stay near 38 high: the cover at 41 is the minimum for a covered 38 mm cooler
(Dev chose covered over an open sleeve).

⚠️ Scaled from the photo (±4 mm), all in `params.scad`:

- Fan-side arc of the cooler outline (`seam_pts`), fan hub (`fan_c`), heatsink outlet span (`fins_y`): fit-check shows them.
- Cable notches (`cable_left`, `cable_right`, `cable_back`).
- The tray is assumed open where the fan stands (the cover's walls stop at the tray edges; over the tray only its
  seam walls come down, onto the lid).

## Design notes

- Lid and cover split along two straight seams 9 mm off the cooler (room for the seam wall and a screw head). The lid
  runs under the seam walls; inside, the cover is open towards the board, so the heat pipe and fan cable cross freely
  and the blower also pulls air through the lid's lattice.
- No floor under the cooler: it stands on the table, which keeps the cover at 41.
- Lid tab joint: horizontal M3 hole (teardrop, tip up in print), nut slot open at the tab's top so the nut drops in.
  Clearance holes 3.4 mm, nut slots 5.7 × 2.7 (slip), mount pockets 5.6 across flats (press), pocket ceilings stepped
  to the hole at > 45° (no bridge).
- `cover` is one solid (outline extruded, inside hollowed, the part over the tray cut away below the lid's top):
  separate walls and top left coplanar faces and the STL wasn't watertight.
