// Shoulder mount: holds the shoulder servo, bolts under a shelf/body plate.
// Print plate down, no supports.
include <lib.scad>

rotate([180, 0, 0]) translate([0, 0, -(mount_stem + plate[2])]) shoulder_mount();
