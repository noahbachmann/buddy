// Gate: print 2 (left under the left wall, right in front of the divider). Prints on its side so the
// gap step lies in the XY plane (most precise). Tune: parts/cad build dispenser gate -D gate_gap=0.4
include <lib.scad>
rotate([0, -90, 0]) translate([0, 0, 0]) gate();
