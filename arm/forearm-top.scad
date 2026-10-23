// Forearm, elbow half: fork around the elbow knuckle.
// Print fork up, cut face on the bed, no supports.
include <lib.scad>

translate([0, 0, -fa_split]) zcut(fa_split, big) forearm();
