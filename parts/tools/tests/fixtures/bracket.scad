// Test fixture: L bracket, clean by default. `defect` plants one known flaw.
//   "round_hole"   horizontal hole without teardrop → overhang at its top
//   "shelf"        10 × 20 mm flat ledge out of the wall at z=20 → 200 mm² roof
//   "missing_hole" one of the two plate holes gone
//   "chamfer45"    45° chamfer under the shelf instead of a flat roof → no overhang
defect = "none";
$fn = 64;

module teardrop(d) rotate([90, 0, 0]) linear_extrude(30, center = true)
  hull() { circle(d = d); rotate(45) square(d / 2); }

difference() {
  union() {
    cube([40, 20, 4]);                               // plate on the bed
    cube([4, 20, 40]);                               // wall at x=0..4
    if (defect == "shelf") translate([4, 0, 20]) cube([10, 20, 4]);
    if (defect == "chamfer45") translate([4, 0, 20]) rotate([-90, 0, 0])
      linear_extrude(20) polygon([[0, 0], [10, 0], [0, 10]]);  // 45° underside
  }
  for (x = defect == "missing_hole" ? [30] : [15, 30]) translate([x, 10, -1]) cylinder(d = 3.4, h = 6);
  // horizontal hole through the wall along X; teardrop rotated so its tip points +Z
  translate([0, 10, 28]) rotate([0, 0, 90])
    if (defect == "round_hole") rotate([90, 0, 0]) cylinder(d = 8, h = 30, center = true);
    else teardrop(8);
}
