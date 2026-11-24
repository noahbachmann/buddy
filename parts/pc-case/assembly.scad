// Whole case in use orientation, ghosts for the tray (with its 28 mm walls) and the cooler. -D explode=20 lifts the parts.
include <lib.scad>
explode = 0;
color("silver", 0.35) board_ghost();
color("orange", 0.35) cooler_ghost();
color("steelblue") translate([0, 0, explode]) lid();
color("seagreen") translate([0, 0, explode * 1.5]) cover();
