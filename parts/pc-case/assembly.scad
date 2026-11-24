// Whole case in use orientation, ghosts for tray + board and cooler. -D explode=20 pulls the parts apart.
include <lib.scad>
explode = 0;
color("silver", 0.35) board_ghost();
color("orange", 0.35) cooler_ghost();
color("dimgray") base();
color("dimgray") cooler_bottom();
color("steelblue") translate([0, 0, explode]) lid();
color("seagreen") translate([0, 0, explode * 1.5]) cooler_top();
