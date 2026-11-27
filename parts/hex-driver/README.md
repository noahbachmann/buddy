# Hex key (M3 socket head)

L-shaped hex key with printed hex tips at both ends for M3 socket head screws (ISO 4762: 2.5 mm hex = 2.89 mm corner to corner).
Two tip sizes, because the printed hex comes out bigger than drawn: try the bigger one first, the smaller if it
won't go in. The flat-printed 2.8 still didn't fit (2026-11-18), so both sit well under the nominal 2.89.

| Part | Tip corner to corner | Across flats |
|---|---|---|
| `hex-driver-26` | 2.6 mm | 2.25 mm |
| `hex-driver-23` | 2.3 mm | 1.99 mm |

Size engraved on the long arm. Bar 8 × 5 mm, long arm ~80 mm, short arm ~32 mm, 10 mm hex at each end. Short arm in
the screw: lever for snugging. Long arm in the screw: reach, spin it fast by the short arm.

## Print

`0-pla.stl`: PLA · 0.12 mm · 4 walls · 100 % · no supports. Lying flat, a hex flat on the bed.

Why flat: standing, every layer seam crossed the hex, so a twist sheared it along one layer. Lying flat, the
perimeters run along the hex and the L can't break at a layer under bending. The fit differs from the old standing
drivers (corners are now layer steps).

⚠️ A printed 2.5 mm hex is weak: standing, twisting sheared it between layers at roughly 0.05–0.1 Nm; flat is
stronger (not measured) but still plastic. Fine for running screws into printed parts and snugging them by feel; tightening into metal or nuts will twist the tip off. A steel 2.5 mm
hex key is the real fix.
