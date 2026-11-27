# PC case skeletons (fit test)

The PC case parts (`..`) cut down to what decides the fit, built from the same `lib.scad` and `params.scad`: change a
parameter there and the skeletons follow. Print them, assemble them on the hardware with the real screws and nuts,
then print the real parts (`../plates`).

| Part | Kept | Plate | PETG (solid, max) |
|---|---|---|---|
| `lid-skeleton` | Rim (10.5 mm: the lid's solid band), the back wall, the 4 slit tabs, the 4 cover nut bosses, one cross with its 7 top-module nut bosses (the press-fit test) | `2-lid-skeleton`, top down, 252 wide: alone | 53 g |
| `cover-top-skeleton` | Top rim (top + upper 3 mm of the walls), corner posts, full-height wall strips at all 9 screws (4 seam bosses, 5 floor tabs) | `1-cover-skeleton` (with cover-bottom-skeleton nested round it), top down | 29 g |
| `cover-bottom-skeleton` | Floor rim, the whole 0.8 fan floor (it sets the fan's height) and the 5 nut tabs | `1-cover-skeleton`, floor down | 18 g |

Print at the real parts' settings (PETG, 0.20 mm, 3 walls): the nut pockets and slit tabs must come out the same.
100 g against 180 g for the real set.

## What to check

1. Lid: the tabs slide into the slits beside the board and stand on the silver plate; the top sits at 34 (6 over the
   tray walls); the edges clear the walls (0.3 mm); the back wall clears the board; the cable notches line up with
   the ribbons and antenna wires.
2. Nuts: press one into each kept top-module pocket and the 4 cover bosses under the lid (press fit), drop the 5 into
   the floor tabs (slip fit).
3. Cover-bottom: the fan stands flat on the 0.8 floor without rocking (the cooler hangs on the CPU too: the floor
   mustn't lever it up); the tabs stay clear under the heatsink.
4. Cover-top: lowers over the cooler onto the floor and onto the lid (0.4 over the cooler), without touching the cooler, the tray wall, the
   silver plate's pads or the antenna wires; all 9 screws go in (4 × M3 × 8 from the top into the lid, 5 × M3 × 8 from
   outside).

Note what doesn't fit (where, by how much) in #87; the fix goes into `../src/params.scad`.

## How they're made

`skel.scad`: each skeleton is `intersection()` of the real part with a keep-region (rims, posts, strips). Keep-regions
never end on a face of the part (the rims reach past the outline, the lid's bars are wider than the lattice bars,
the right-wall strips end mid-slit): a shared face leaves a pinched STL or a sliver. Every STL is checked for walls
thinner than 0.8 mm.
