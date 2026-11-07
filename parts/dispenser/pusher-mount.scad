// Pusher mount: holds the 17HS4401 and the rods' right ends on the wall's outer face. Print inner face down.
include <lib.scad>
translate([0, 0, -right_wall[1]]) rotate([0, -90, 0]) pusher_mount();
