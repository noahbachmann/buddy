// Pile B's front border (slide-in, from the front over the floor until its lip stops at the floor's front edge; 2 M3
// from below). Prints on its divider end: the card face is a wall, no overhang.
include <lib.scad>
rotate([0, -90, 0]) translate([-(slit_x1 + insert_fit), 0, 0]) b_insert();
