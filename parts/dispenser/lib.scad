// Dispenser roller: shared geometry, built in dispenser.stl's frame (params.scad).
// Part files include this and turn their piece into print orientation.
include <BOSL2/std.scad>
include <BOSL2/gears.scad>
include <params.scad>

eps = 0.01;
bed_bot = bed_top - bed_t;
rod_xs  = [idler_x, drive_x];
drum_len = drum_y[0] - drum_y[1];
// motor hole skipped: it would cut into the drive rod's blind hole
motor_hole_offs = [[-1, -1], [1, -1], [-1, 1]] * motor_holes / 2;
plate_cy = [(back_y[0] + back_y[1]) / 2, (front_y[0] + front_y[1]) / 2];
floor_screws = [for (x = [20, 60], y = plate_cy) [x, y]];   // from below through the box floor into the plates
rib_bot = drum_z - cord_c + cord_s / 2 + 1.5;              // 1.5 above the belts' bottom run

// --- Helpers --------------------------------------------------------------------------
// Teardrop for a horizontal hole: point towards +Z (tip = 1) or -Z (tip = -1).
module teardrop2d(d, tip = 0) {
  if (tip == 0) circle(d = d);
  else rotate(tip < 0 ? 180 : 0) hull() { circle(d = d); rotate(45) square(d / 2); }
}
// Hole along Y from y1 down to y0, centre (x, z).
module hole_y(x, z, d, y0, y1, tip = 0)
  translate([x, y1, z]) rotate([90, 0, 0]) linear_extrude(y1 - y0) teardrop2d(d, tip);
// Box from corner ranges.
module box(xr, yr, zr) translate([xr[0], yr[0], zr[0]]) cube([xr[1] - xr[0], yr[1] - yr[0], zr[1] - zr[0]]);

// --- Placement --------------------------------------------------------------------------
// Drum local frame: axis +Z, z 0 = back end (gear end of the drive drum).
module drum_tf(x) translate([x, drum_y[0], drum_z]) rotate([90, 0, 0]) children();
// Motor local frame: face at z 0, shaft +Z, D-flat on +X, connector on -Y (as in dispenser.stl
// with the axis moved to the origin). Placed: shaft to the front, connector to the left.
module motor_tf() translate([motor_x, motor_face_y, motor_z]) rotate([90, 0, 0]) rotate(-90) children();

// --- Drums -------------------------------------------------------------------------------------
module drum(gear) {
  difference() {
    union() {
      if (gear)
        spur_gear(mod = gear_mod, teeth = drum_teeth, thickness = gear_w, profile_shift = 0,
                  backlash = gear_backlash, anchor = BOTTOM);
      translate([0, 0, gear ? gear_w - eps : 0]) cylinder(r = rim_r, h = drum_len - (gear ? gear_w - eps : 0));
    }
    // grooves: teardrop profile, the drum prints standing (+Z up)
    for (y = cord_ys) translate([0, 0, drum_y[0] - y])
      rotate_extrude() translate([groove_root + groove_r, 0]) teardrop2d(2 * groove_r, 1);
    translate([0, 0, -eps]) cylinder(d = drum_bore, h = drum_len + 2 * eps);
    seat = bearing[1] + press;
    for (z = [-eps, drum_len - bearing[2]])
      translate([0, 0, z]) cylinder(d = seat, h = bearing[2] + eps);
    // 45° cone above the lower seat instead of a flat roof
    translate([0, 0, bearing[2] - eps]) cylinder(d1 = seat, d2 = drum_bore, h = (seat - drum_bore) / 2);
  }
}

// --- Motor gear: on the 4023's D-shaft, M3 set screw onto the flat, nut slot from the top ----------
gear_z0 = motor_face_y - gear_y[1];       // motor local z of the gear's back face
hub = [14, 6];                            // diameter, length
module motor_gear() {
  hz = gear_z0 + gear_w + hub[1] / 2;
  difference() {
    union() {
      translate([0, 0, gear_z0])
        spur_gear(mod = gear_mod, teeth = motor_teeth, thickness = gear_w, profile_shift = 0,
                  backlash = gear_backlash, anchor = BOTTOM, gear_spin = 180 / motor_teeth);
      translate([0, 0, gear_z0 + gear_w - eps]) cylinder(d = hub[0], h = hub[1] + eps);
    }
    // D-bore: 5 mm shaft, flat 2.0 from the axis
    translate([0, 0, gear_z0 - eps]) linear_extrude(gear_w + hub[1] + 2 * eps)
      intersection() { circle(d = 5.15); translate([-5, -5]) square([5 + 2.1, 10]); }
    // set screw onto the flat (+X) and its nut slot, open to the hub's end face
    translate([0, 0, hz]) rotate([0, 90, 0]) cylinder(d = screw_clear - 0.2, h = hub[0]);
    translate([3.2, -5.8 / 2, hz - 5.8 / 2]) cube([2.6, 5.8, hub[1]]);
  }
}

// --- Gate: bar over the belt with one card's gap, standing on the bed over both plates ---------------
// Local: bar at x 0..gate_w (outer face x 0). Square pegs into sockets: no screws, swapping a gate
// to tune the gap takes seconds (the left one sits under the left wall, no tool reaches there).
peg = [gate_w, 3, 4];             // full bar width: prints on the bed
peg_fit = 0.15;
module gate() {
  bar_z = belt_top + gate_gap;
  box([0, gate_w], [front_y[0], back_y[1]], [bar_z, gate_top]);
  for (yr = [back_y, front_y]) box([0, gate_w], yr, [bed_top, gate_top]);
  for (y = plate_cy) translate([0, y - peg[1] / 2, bed_top - peg[2]]) cube(peg + [0, 0, eps]);
}
module gate_placed(i) {
  if (i == 0) translate([gate_xs[0], 0, 0]) gate();
  else translate([divider_x - 0.5, 0, 0]) mirror([1, 0, 0]) gate();
}
function gate_x(i) = i == 0 ? gate_xs[0] + gate_w / 2 : divider_x - 0.5 - gate_w / 2;   // peg centre

// --- Frame: back plate (holds the 4023) + bed + ribs. Prints upside down: bed on the build plate. ---
module plate_profile(top) {
  // left of step_x it stands on the table, right of it on the box floor
  translate([frame_x[0], 0]) square([step_x - frame_x[0], top]);
  translate([step_x, floor_top]) square([frame_x[1] - step_x, top - floor_top]);
}
module xz_plate(yr, top = bed_top)
  translate([0, yr[1], 0]) rotate([90, 0, 0]) linear_extrude(yr[1] - yr[0]) plate_profile(top);
// gate peg sockets in a plate's top
module peg_sockets(y)
  for (i = [0, 1]) translate([gate_x(i), y, bed_top - peg[2] - 0.5]) linear_extrude(peg[2] + 1.5)
    square([peg[0] + 2 * peg_fit, peg[1] + 2 * peg_fit], center = true);
// rib i, y range: the right rib stops short of the gear pair
function rib_y(i) = [front_y[1], i == 2 ? gear_y[0] - 0.5 : back_y[0] + eps];
function rib_screw(i) = [rib_xs[i] + rib_t / 2, (rib_bot + bed_bot) / 2];

module frame() {
  difference() {
    union() {
      xz_plate(back_y, bed_bot + 1);   // ends inside the bed: no coplanar top faces
      box([frame_x[0] + 1, frame_x[1]], [front_y[1], back_y[1]], [bed_bot, bed_top]);   // 1 mm short: no coplanar end face
      for (i = [0 : 2]) box([rib_xs[i], rib_xs[i] + rib_t], rib_y(i), [rib_bot, bed_bot + eps]);
      // bosses: touch only the bearings' inner races
      for (x = rod_xs) translate([x, back_y[0] + eps, drum_z]) rotate([90, 0, 0]) cylinder(d = boss_d, h = boss + eps);
    }
    // drums and the drum gear: nothing of the bed over them
    w = rim_r + open_clear;
    box([drive_x - w, frame_x[1] + 1], [drum_y[1] - 1, gear_y[0] - 0.5], [bed_bot - 1, bed_top + 1]);
    box([frame_x[0] - 1, idler_x + w], [drum_y[1] - 1, drum_y[0] + 0.5], [bed_bot - 1, bed_top + 1]);
    gr = gear_mod * (drum_teeth + 2) / 2 + open_clear;
    box([drive_x - gr, frame_x[1] + 1], [gear_y[0] - 0.5, drum_y[0] + 0.5], [bed_bot - 1, bed_top + 1]);
    box(cam_window[0], cam_window[1], [bed_bot - 1, bed_top + 1]);
    // rods: blind in the back plate
    for (x = rod_xs) hole_y(x, drum_z, rod_hole, back_y[0] - boss - eps, back_y[0] + plate_t - 1, -1);
    // 4023: boss recess and shaft hole, 3 screws from the front with heads sunk (the drum gear runs there)
    translate([motor_x, 0, motor_z]) {
      hole_y(0, 0, motor_boss[0] + 0.6, back_y[1] - motor_boss[1] - 0.3, back_y[1] + eps, -1);
      hole_y(0, 0, 9, back_y[0] - eps, back_y[1] + eps, -1);
      for (o = motor_hole_offs) {
        hole_y(o[0], o[1], screw_clear, back_y[0] - eps, back_y[1] + eps, -1);
        hole_y(o[0], o[1], screw_head, back_y[0] - eps, back_y[0] + 3.2, -1);
      }
    }
    peg_sockets(plate_cy[0]);
    // front plate screws into the rib ends, floor screws from below
    for (i = [0 : 2]) hole_y(rib_screw(i)[0], rib_screw(i)[1], screw_pilot, front_y[1] - eps, front_y[1] + 10);
    for (p = floor_screws) if (p[1] > -90) translate([p[0], p[1], floor_top - eps]) cylinder(d = screw_pilot, h = 10);
  }
}

// --- Front plate: separate so the closed O-rings slide on from the front. Prints flat. --------------
module front_plate() {
  difference() {
    union() {
      xz_plate(front_y);
      for (x = rod_xs) translate([x, front_y[1] - eps, drum_z]) rotate([-90, 0, 0]) cylinder(d = boss_d, h = boss + eps);
    }
    for (x = rod_xs) {
      hole_y(x, drum_z, rod_hole, front_y[0] - eps, front_y[1] + boss + eps);
      // keeper: M3 screw + washer beside the rod end holds the rod in
      hole_y(x + (x < 0 ? 5 : -5), drum_z, screw_pilot, front_y[0] - eps, front_y[1] + eps);
    }
    // screwdriver access to the motor screws
    for (o = motor_hole_offs) hole_y(motor_x + o[0], motor_z + o[1], 7, front_y[0] - eps, front_y[1] + eps);
    for (i = [0 : 2]) hole_y(rib_screw(i)[0], rib_screw(i)[1], screw_clear, front_y[0] - eps, front_y[1] + eps);
    peg_sockets(plate_cy[1]);
    for (p = floor_screws) if (p[1] < -90) translate([p[0], p[1], floor_top - eps]) cylinder(d = screw_pilot, h = 10);
  }
}

// --- Bought parts (assembly and the combined STL only) -------------------------------------------
rod_len = back_y[0] + plate_t - 1 - front_y[0];   // blind hole bottom -> front plate outer face
module rods() for (x = rod_xs) hole_y(x, drum_z, rod_d, front_y[0], front_y[0] + rod_len);
module belts() for (y = cord_ys) translate([0, y + cord_s / 2, 0]) rotate([90, 0, 0])
  linear_extrude(cord_s) difference() {
    hull() for (x = rod_xs) translate([x, drum_z]) circle(r = cord_out);
    hull() for (x = rod_xs) translate([x, drum_z]) circle(r = groove_root);
  }
module motor_4023() motor_tf() translate([-35, 25, -28.4]) import("ref/m4023.stl");
