// Upper arm, elbow half: houses the elbow servo.
// Print knuckle up, cut face on the bed, no supports.
include <lib.scad>

rotate([180, 0, 0]) translate([0, 0, -ua_split]) zcut(-big, ua_split) upper_arm();
