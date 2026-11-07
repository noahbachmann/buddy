// Roller frame: back plate (holds the 4023), front plate, cross walls, card bed.
// Print upside down: bed on the plate, everything else grows up from it, no supports.
include <lib.scad>
translate([0, 0, bed_top]) rotate([180, 0, 0]) frame();
