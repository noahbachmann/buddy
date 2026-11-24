// Roller frame: front plate, bed, ribs. Print upside down: bed and plate top on the build plate, no supports.
include <lib.scad>
translate([0, 0, bed_top]) rotate([180, 0, 0]) frame();
