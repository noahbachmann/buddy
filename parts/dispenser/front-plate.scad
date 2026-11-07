// Front plate of the roller frame, screwed onto the rib ends. Print outer face down.
include <lib.scad>
translate([0, 0, -front_y[0]]) rotate([90, 0, 0]) front_plate();
