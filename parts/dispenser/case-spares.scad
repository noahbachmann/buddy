// Case spares: case.scad with less filament (lib: case_spares). Same print: on its back, no supports.
// Rear top cut down behind the pusher motor, windows in the back wall (bed) and the lift's floor, truss feet.
include <lib.scad>
rotate([-90, 0, 0]) translate([0, -back_out, 0]) case_spares();
