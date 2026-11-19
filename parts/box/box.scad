// Box open on one side. Prints upside down: closed top on the bed, open side up (no 62 × 42 mm roof to bridge).
include <params.scad>

difference() {
    cube([length, width, height]);
    translate([wall, wall, top]) cube([length - 2 * wall, width - 2 * wall, height]);
}
