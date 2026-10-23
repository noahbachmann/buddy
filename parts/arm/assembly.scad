// Whole arm, posed. Preview only (F5), not for printing.
// Shoulder pivot at the origin, +X = front.
include <lib.scad>

shoulder = 30;  // forward raise, degrees
elbow    = 60;  // flexion, degrees

color("silver") shoulder_mount();
%servo_ghost();
rotate([0, -shoulder, 0]) {
  color("orange") translate([0, 0, -upper_arm_len]) upper_arm();
  translate([0, 0, -upper_arm_len]) {
    %servo_ghost();
    rotate([0, -elbow, 0]) color("teal") forearm();
  }
}
