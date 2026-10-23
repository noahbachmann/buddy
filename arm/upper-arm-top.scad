// Upper arm, shoulder half: fork around the shoulder knuckle.
// Print fork up, cut face on the bed, no supports.
include <lib.scad>

translate([0, 0, -ua_split]) zcut(ua_split, big) upper_arm();
