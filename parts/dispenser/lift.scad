// Lift: plate + back stop + nut tab (replaces Dev's lift). Prints as placed, plate down; support under the
// tab (Dev's design).
include <lib.scad>
translate([0, 0, -(lift_min - lift_t)]) lift_part(lift_min);
