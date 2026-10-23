// Coupler rings that join the arm halves (glue or M3 screws). Print standing.
include <lib.scad>

translate([-60, 0, 0]) coupler(upper_arm_profile, ua_split);
translate([50, 0, 0]) coupler(forearm_profile, fa_split);
