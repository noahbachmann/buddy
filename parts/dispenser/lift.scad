// Lift: plate + back stop + nut tab (replaces Dev's lift). Prints as placed, plate down; the tab sits on 2 gussets
// and bridges between them.
include <lib.scad>
translate([0, 0, -(lift_min - lift_t)]) lift_part(lift_min);
