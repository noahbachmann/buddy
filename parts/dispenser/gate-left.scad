// Left gate (under the left wall). Prints on its side so the gap step lies in the XY plane (most precise).
// Tune: parts/cad build dispenser gate-left -D gate_gap=0.4 (the right gate is part of the box)
include <lib.scad>
rotate([0, -90, 0]) gate();
