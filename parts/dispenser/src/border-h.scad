// Pile H's front border (slide-in, from the front into the case until it stops at the right gate; 2 M3 through the
// left wall). Prints on its left end: the card face is a wall, no overhang.
include <lib.scad>
rotate([0, -90, 0]) translate([-(left_wall[1] + insert_fit), 0, 0]) h_insert();
