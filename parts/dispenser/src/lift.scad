// Lift: plate + back stop + nut tab (replaces Dev's lift). Prints as placed, plate down; the tab needs support:
// Bambu Studio's auto supports (plate 3).
include <lib.scad>
translate([0, 0, -(lift_min - lift_t)]) lift_part(lift_min);
