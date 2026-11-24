// Back plate of the roller: holds the 4023, the back pins and the rib pockets. Print upside down, top edge on the plate.
include <lib.scad>
translate([0, 0, bed_top]) rotate([180, 0, 0]) back_plate();
