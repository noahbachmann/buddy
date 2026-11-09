// Lift: plate + back stop + nut tab (replaces Dev's lift). Prints as placed, plate down; the tab's support is
// lift-support.scad, printed with it from plates/3-pla.stl.
include <lib.scad>
translate([0, 0, -(lift_min - lift_t)]) lift_part(lift_min);
