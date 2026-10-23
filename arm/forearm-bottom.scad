// Forearm, wrist half: wrist plate for the hand (phase 3).
// Print wrist plate down, no supports.
include <lib.scad>

translate([0, 0, forearm_len]) zcut(-big, fa_split) forearm();
