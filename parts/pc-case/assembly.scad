// Whole case in use orientation, ghosts for the tray walls, the silver plate and the cooler. -D explode=20 lifts the parts.
include <lib.scad>
explode = 0;
color("silver", 0.35) tray_ghost();
color("orange", 0.35) cooler_ghost();
color("steelblue") translate([0, 0, explode]) lid();
color("seagreen") translate([0, 0, explode * 1.5]) cover();
