// Breakaway support under the lift's tab, in lift.scad's coordinates. Only for the plate file (cad.toml [plates]).
include <lib.scad>
translate([0, 0, -(lift_min - lift_t)]) lift_support(lift_min);
