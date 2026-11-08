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
// Hole along Z from z0 to z1 at (x, y), teardrop towards -Y: up when the case prints on its back.
module hole_z(x, y, d, z0, z1) translate([x, y, z0]) linear_extrude(z1 - z0) rotate(180) teardrop2d(d, 1);
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
module motor_tf() translate([motor_x, motor_face_y, motor_z]) rotate([90, 0, 0]) children();   // connector down

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
module gate_placed() translate([gate_xs[0], 0, 0]) gate();

// Right gate, part of the box (Dev's change), x gate_in .. the divider: bar (underside = the divider block's underside,
// gate_gap above the belts) with pile H's wedge on it (inner face from the bar's top up to the divider's top edge),
// legs over the frame plates, the lip under the card path between the drive drum and the divider, and the slit's ends.
// Lip profile (x-z): flat top, drum side = clearance arc, then the arc's 45° tangent down to the divider.
lip_top = belt_top - lip_drop;
lip_r   = gear_mod * (drum_teeth + 2) / 2 + lip_clear;   // the drum gear's tips (they reach 0.6 above the belts)
lip_y   = [front_y[1] - 1, back_y[0] + 1];   // into both legs by 1
// a card left on the lip must sit above the divider's lower part, or the next card's edge hits the divider under the
// slot: thick cards (gap = 1.5 card_t) push the belts and the lip down, then lower lip_drop
assert(lip_top >= slot_bot + 0.05, str("lip top ", lip_top, " not above the divider (z ", slot_bot, "): card_t too big for lip_drop"));
// x-y region outside the slit's funnel on side s (0 front, 1 back), wide enough for gate and divider
slit_x1 = divider_x + divider_t;
function slit_edge(s, x) = s == 0 ? card_yc - card[1] / 2 - slit_play - (slit_x1 - x)
                                  : card_yc + card[1] / 2 + slit_play + (slit_x1 - x);
module slit_end(s) let(x0 = gate_in - 1, far = s == 0 ? slot_y[0] - 10 : slot_y[1] + 10)
  polygon([[x0, slit_edge(s, x0)], [slit_x1, slit_edge(s, slit_x1)], [slit_x1, far], [x0, far]]);
// Dev's change: the divider slot filled outside the funnel (its upper block was held only by the back wall)
module slot_ends() for (s = [0, 1]) intersection() {
  translate([0, 0, slot_bot - 0.5]) linear_extrude(slot_top - slot_bot + 1) slit_end(s);
  box([divider_x, slit_x1], [slot_y[0], slot_y[1] + 0.5], [slot_bot - 1, slot_top + 1]);
}

module right_gate() {
  a0 = atan2(lip_top - lip_edge - drum_z, sqrt(lip_r ^ 2 - (lip_top - lip_edge - drum_z) ^ 2));
  x45 = drive_x + lip_r * cos(45);
  z45 = drum_z + lip_r * sin(45);
  arc = [for (a = [45 : (a0 - 45) / 16 : a0]) [drive_x + lip_r * cos(a), drum_z + lip_r * sin(a)]];
  edge = arc[16][0];
  bar_z = belt_top + gate_gap;
  dx = divider_x;
  leg_z = bed_top + 0.3;   // over the frame plates (they slide in under the legs)
  difference() {
    union() {
      // bar + wedge, 1 into the divider block above its underside, from the front leg into the back wall (the case
      // prints on its back: a free back end would overhang)
      translate([0, back_wall + 0.5, 0]) rotate([90, 0, 0]) linear_extrude(back_wall + 0.5 - front_y[0])
        polygon([[gate_in, bar_z], [dx, bar_z], [dx, bar_z + 0.3], [dx + 1, bar_z + 0.3], [dx + 1, div_top - 0.5],
                 [dx - 0.2, div_top - 0.5], [gate_in, gate_top]]);
      for (yr = [back_y, front_y]) box([gate_in, dx], yr, [leg_z, bar_z + 1.5]);
      difference() {   // lip, into both legs above the plates
        translate([0, lip_y[1], 0]) rotate([90, 0, 0]) linear_extrude(lip_y[1] - lip_y[0])
          polygon(concat([[edge, lip_top], [dx, lip_top], [dx, z45 - (dx - x45)]], arc));
        // below the legs' underside: over the front plate, and over the bed behind the drum and the back plate
        for (y = [[lip_y[0] - 1, front_y[1]], [drum_y[0], lip_y[1] + 1]]) box([gate_in - 1, dx + 1], y, [0, leg_z]);
      }
      // slit ends: fill lip..bar outside the funnel
      for (s = [0, 1]) intersection() {
        translate([0, 0, lip_top - 0.5]) linear_extrude(bar_z - lip_top + 1) slit_end(s);
        box([gate_in, dx], [lip_y[0], lip_y[1]], [0, gate_top]);
      }
    }
    // the drums slide on from the front: clearance for the gear's tips along the whole way
    translate([drive_x, lip_y[1], drum_z]) rotate([90, 0, 0]) cylinder(r = lip_r - 0.02, h = lip_y[1] - (front_y[0] - 1));
  }
}
function gate_x() = gate_xs[0] + gate_w / 2;   // left gate's peg centre

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
  translate([gate_x(), y, bed_top - peg[2] - 0.5]) linear_extrude(peg[2] + 1.5)
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

// === Pusher ======================================================================================
// Hole along X from x0 to x1, centre (y, z); tip = 1: teardrop pointing up (+Z), front: to -Y instead (the case's
// print direction).
module hole_x(y, z, d, x0, x1, tip = 0, front = false)
  translate([x0, y, z]) rotate([0, 90, 0]) linear_extrude(x1 - x0) rotate(front ? 180 : 90) teardrop2d(d, front ? 1 : tip);

// Plate side profile (x, z): chamfered lower edge, sharp-ish on the push side to catch the bottom card
function pu_profile(h = push_h) = let(b = push_lift, t = push_lift + h, x0 = right_wall[0], x1 = right_wall[0] + push_t,
                            c = push_cham)
  [[x0, b + c[0]], [x0 + c[0], b], [x1 - c[1], b], [x1, b + c[1]], [x1, t], [x0, t]];

// Carriage at home (push = 0) .. end of stroke (push = 1): plate + bar + nut tab, like the lift turned 90°.
// Print: as placed, bottom down.
module pusher(push = 0) translate([-push * push_stroke, 0, 0]) difference() {
  b = push_lift;
  c = push_cham[1];
  // front strip: bar, tab and gusset as one side profile (x, z), so no coplanar faces meet in the union
  front = [[tab_x[0], b], [bar_x[1] - c, b], [bar_x[1], b + c], [bar_x[1], b + bar_h], [tab_x[1] + 15, b + bar_h],
           [tab_x[1], b + (bar_h + 15)], [tab_x[1], pu_screw_z + (nut_flange[0] / 2 + 1)],
           [tab_x[0], pu_screw_z + (nut_flange[0] / 2 + 1)]];
  union() {
    for (r = [[push_y[0], push_y[1], pu_profile()], [bar_y[0], bar_y[1], front]])
      translate([0, r[0], 0]) mirror([0, 1, 0]) rotate([90, 0, 0]) linear_extrude(r[1] - r[0]) polygon(r[2]);
    box([right_wall[0] + 1, right_wall[0] + (push_t - 1)], [push_y[0] - 1, push_y[0] + 1], [b + 2, b + (bar_h - 1)]);  // joint
  }
  // nut through the tab, flange screwed to its right face (pushing presses the flange onto the tab)
  hole_x(pu_y, pu_screw_z, nut_d + 0.4, tab_x[0] - eps, tab_x[1] + eps, 1);
  for (s = [-1, 1]) hole_x(pu_y, pu_screw_z + s * nut_pcd / 2, screw_pilot, tab_x[0] - eps, tab_x[1] + eps);
}

// Dev's change to the box: the right wall extended forward around the motor face (45° underneath, prints upright),
// the boss's hole and the motor's 4 screw holes through it.
m44_hole_offs = [for (a = [-1, 1], b = [-1, 1]) [a, b] * m44_holes / 2];
module case_add() difference() {   // additions to Dev's box: wall extension, slot ends, motor holds, borders, right gate, feet
  dy = ext_y[1] - ext_y[0];
  union() {
    hull() {
      box(right_wall, [ext_y[0], ext_y[1] + 1], ext_z);
      box(right_wall, [ext_y[1], ext_y[1] + 1], [ext_z[0] - dy, ext_z[0]]);
    }
    slot_ends();
    roller_hold();
    pusher_hold();
    h_back_border();
    right_gate();
    feet();
  }
  wall_holes();
}
module wall_holes() {
  hole_x(pu_y, pu_screw_z, m44_boss[0] + 0.6, right_wall[0] - 1, right_wall[1] + 1, front = true);
  for (o = m44_hole_offs)
    hole_x(pu_y + o[0], pu_screw_z + o[1], screw_clear, right_wall[0] - 1, right_wall[1] + 1, front = true);
}
// Rails under the floor along the left and right edges, back wall to front edge: the box stands on them, the lift
// motor hangs between them. Open front and back: cables, and the motor reaches past the back wall.
module feet() for (x = [0, right_wall[0]]) box([x, x + feet_t], [box_front, back_out], [-feet_h, 0.5]);

// Dev's cuts in the box (Onshape), here for the preview and the combined STL.
// 4023: the hold carries the motor from below, from the frame's back plate through the wall to the motor's back end
m23_box = [[motor_x - motor_sq / 2, motor_x + motor_sq / 2], [motor_face_y, motor_face_y + motor_body],
           [motor_z - motor_sq / 2, motor_z + motor_sq / 2]];
// It ends at the wall's outer face (the case prints on it): the motor's last 3 mm overhang.
module roller_hold() box(m23_box[0], [m23_box[1][0] + hold_gap, min(m23_box[1][1], back_out)], [0, m23_box[2][0]]);
// 17HS4401 on the right wall: a wedge under its body, 45° underside, and 45° along the wall towards the back (the case
// prints on its back, -Y up: a flat back face would overhang)
module pusher_hold() {
  top = pu_screw_z - m44_sq / 2;
  x0 = right_wall[1] - 1;
  x1 = right_wall[1] + m44_body;
  y1 = pu_y + pu_hold[0] / 2;
  hull() {
    translate([0, y1, 0]) rotate([90, 0, 0]) linear_extrude(pu_hold[0])
      polygon([[x0, top - (x1 - x0) - pu_hold[1]], [x0, top], [x1, top], [x1, top - pu_hold[1]]]);
    box([x0, x0 + 0.01], [y1, y1 + (x1 - x0)], [top - (x1 - x0) - pu_hold[1], top]);
  }
}
// Pile H's borders: left wall .. divider, from just above the left wall's lower edge to the divider top; inner faces
// 0.5 off the cards, leaning out like the lift's slope.
h_bord_z = [left_wall_bottom + 0.5, div_top];
h_lean   = (h_bord_z[1] - h_bord_z[0]) * stop_lean / stop_h;
// Back: part of the case, solid back to the wall (through the right gate's wedge): the case prints on its back, a
// free outer face would overhang.
module h_back_border() let(zb = h_bord_z[0], zt = h_bord_z[1], y1 = card_yc + card[1] / 2 + 0.5)
  translate([left_wall[1] - 1, 0, 0]) rotate([90, 0, 90]) linear_extrude(divider_x - (left_wall[1] - 1))
    polygon([[y1, zb], [y1 + h_lean, zt], [back_wall + 0.5, zt], [back_wall + 0.5, zb]]);

// Front borders of both piles: slide-in parts (their card faces would overhang with the case on its back). Each fills
// the front strip in front of its pile, flush with the box front. Profiles (y, z), extruded along X.
// H: left wall .. the right gate's wedge; the step beside the gate's front face is its stop, 2 M3 through the left wall.
function h_insert_profile() = let(zb = h_bord_z[0], zt = h_bord_z[1], y1 = card_yc - card[1] / 2 - 0.5)
  [[box_front, zb], [y1, zb], [y1 - h_lean, zt], [box_front, zt]];
module h_insert() {
  f = insert_fit;
  difference() {
    translate([left_wall[1] + f, 0, 0]) rotate([90, 0, 90]) linear_extrude(divider_x - f - (left_wall[1] + f))
      polygon(h_insert_profile());
    // the right gate's bar and wedge (inner face from (gate_in, gate_top) to (divider_x - 0.2, div_top - 0.5)), + fit
    translate([0, back_wall, 0]) rotate([90, 0, 0]) linear_extrude(back_wall - (front_y[0] - f))
      polygon([[gate_in - f, h_bord_z[0] - 1], [gate_in - f, gate_top], [divider_x - 0.2 - f, div_top - 0.5],
               [divider_x - 0.2 - f, div_top + 1], [divider_x + 1, div_top + 1], [divider_x + 1, h_bord_z[0] - 1]]);
    for (s = insert_screws_h) hole_x(s[0], s[1], screw_pilot, left_wall[1], left_wall[1] + 10);
  }
}
// B: divider .. right wall, on the floor; a lip over the floor's front edge is its stop, 2 M3 from below.
// Inner face vertical 0.5 in front of the lift plate up to its lowest top, then leaning forward like the slope.
function b_insert_profile() = let(y1 = lift_front - 0.5, lean = (slot_bot - lift_min) * stop_lean / stop_h,
                                  bf = box_front)
  [[bf - 2, 0.5], [bf, 0.5], [bf, floor_top], [y1, floor_top], [y1, lift_min], [y1 - lean, slot_bot], [bf, slot_bot],
   [bf, floor_top + 5], [bf - 2, floor_top + 5]];
module b_insert() {
  f = insert_fit;
  difference() {
    translate([slit_x1 + f, 0, 0]) rotate([90, 0, 90]) linear_extrude(right_wall[0] - f - (slit_x1 + f))
      polygon(b_insert_profile());
    for (s = insert_screws_b) translate([s[0], s[1], floor_top - eps]) cylinder(d = screw_pilot, h = 10);
  }
}
// Dev's box as drawn, its front cut back
module box_trimmed() difference() {
  import("ref/box.stl");
  box([left_wall[0] - 1, right_wall[1] + 1], [box_front - 20, box_front], [-1, 201]);
}

module box_cuts() {
  // 4023: window through the back wall, its floor = the hold's top; channel for the connector and cable, out below
  box([m23_box[0][0] - m23_window, m23_box[0][1] + m23_window], [back_wall - 1, 1],
      [m23_box[2][0], m23_box[2][1] + m23_window]);
  box([motor_x - cable_slot[0] / 2, motor_x + cable_slot[0] / 2],
      [m23_box[1][1] - m23_conn[2] - cable_slot[1], m23_box[1][1] + 1], [-1, m23_box[2][0] + 1]);
  for (n = [notch_lo, notch_hi]) box([right_wall[0] - 1, right_wall[0] + push_t], n[0], n[1]);
  wall_holes();
  hole_x(pu_y, pu_screw_z, screw_hole, left_wall[0] - 1, left_wall[1] + 1, front = true);
  // lift motor under the floor: boss hole + the 2 front screw holes (the back 2 are under the wall)
  hole_z(lift_axis[0], lift_axis[1], m44_boss[0] + 0.6, -1, floor_top + 1);
  for (o = m44_hole_offs) if (o[1] < 0) hole_z(lift_axis[0] + o[0], lift_axis[1] + o[1], screw_clear, -1, floor_top + 1);
  // frame to the floor, border inserts
  for (p = floor_screws) hole_z(p[0], p[1], screw_clear, -1, floor_top + 1);
  for (s = insert_screws_b) hole_z(s[0], s[1], screw_clear, -1, floor_top + 1);
  for (s = insert_screws_h) hole_x(s[0], s[1], screw_clear, left_wall[0] - 1, left_wall[1] + 1, front = true);
}
// The printed case: Dev's box with every change, as it sits (case.scad turns it onto its back)
module case() difference() { union() { box_trimmed(); case_add(); } box_cuts(); }

// Bought parts. 17HS4401 in its own frame: face at z 0, body below, shaft up; coupling on the shaft.
module m44_local() {
  translate([0, 0, -m44_body]) linear_extrude(m44_body) offset(r = 3) offset(delta = -3) square(m44_sq, center = true);
  translate([0, 0, -eps]) cylinder(d = m44_boss[0], h = m44_boss[1] + eps);
  cylinder(d = m44_shaft[0], h = m44_shaft[1]);
}
module coupling_local() translate([0, 0, m44_shaft[1] - coupling[1] / 2]) cylinder(d = coupling[0], h = coupling[1]);
module pu_tf() translate([m44_face, pu_y, pu_screw_z]) rotate([0, -90, 0]) children();
module lift_tf() translate([lift_axis[0], lift_axis[1], lift_face]) children();

module m4401() pu_tf() m44_local();
module pu_coupling() pu_tf() coupling_local();
module pu_screw() hole_x(pu_y, pu_screw_z, 8, screw_x[0], screw_x[1]);
module pu_nut(push = 0) translate([-push * push_stroke, 0, 0]) {
  hole_x(pu_y, pu_screw_z, nut_d, nut_x[0], nut_x[1] - nut_flange[1] + eps);
  hole_x(pu_y, pu_screw_z, nut_flange[0], nut_x[1] - nut_flange[1], nut_x[1]);
}
module lift_motor() lift_tf() m44_local();
module lift_coupling() lift_tf() coupling_local();
module lift_screw() lift_tf() translate([0, 0, m44_shaft[1]]) cylinder(d = 8, h = lift_screw_top - m44_shaft[1]);

// --- Lift (printed): plate + slope + Dev's nut tab. Plate top at `top`. Print: as placed, plate down; the tab needs
// support (Dev's design). One intersection, no unions: side profile (along X) ∩ front profile (along Y: straight sides
// from the plate's corners to the tab's) ∩ plan (along Z: everything up to just under the tab, the tab's outline above).
assert(card_yc + card[1] / 2 <= gear_y[0] - 1, "cards reach the drum gear");
assert(lift_axis[1] - nut_flange[0] / 2 - (riser_y + stop_lean + stop_t) >= 0.5 - 1e-9, "nut flange hits the slope");
module lift_part(top) {
  b = top - lift_t;
  st = top + stop_h;
  zt = st - tab_t;                       // tab underside = nut flange top
  yb = riser_y + stop_lean + stop_t;     // slope wall's back face (vertical, below the tab)
  ax = lift_axis;
  sx = [ax[0] - stop_w / 2, ax[0] + stop_w / 2];
  // front profile: straight sides from just outside the plate's top corners (0.3, so they don't meet its edges) to
  // 0.2 outside the tab 1 mm under it; above that vertical, the plan's 28 mm outline makes the tab's sides (from
  // zt - 0.5: no edge of one profile may lie on another's face)
  zs = zt - 1;
  xo = [lift_x[0] - 1, lift_x[1] + 1];
  side = function(i, z) let(x0 = i == 0 ? lift_x[0] - 0.3 : lift_x[1] + 0.3, x1 = i == 0 ? sx[0] - 0.2 : sx[1] + 0.2)
    x0 + (x1 - x0) * (z - top) / (zs - top);
  zx = function(i) top + (zs - top) * (xo[i] - (i == 0 ? lift_x[0] - 0.3 : lift_x[1] + 0.3))
                                     / ((i == 0 ? sx[0] - 0.2 : sx[1] + 0.2) - (i == 0 ? lift_x[0] - 0.3 : lift_x[1] + 0.3));
  difference() {
    intersection() {
      translate([lift_x[0], 0, 0]) rotate([90, 0, 90]) linear_extrude(lift_x[1] - lift_x[0])
        polygon([[lift_front, b], [yb, b], [yb, zt], [lift_tab_y + 1, zt], [lift_tab_y + 1, st], [yb - stop_t, st],
                 [riser_y, top], [lift_front, top]]);
      translate([0, lift_tab_y + 2, 0]) rotate([90, 0, 0]) linear_extrude(lift_tab_y + 2 - (lift_front - 1))
        polygon([[xo[0], b - 1], [xo[1], b - 1], [xo[1], zx(1)], [side(1, zs), zs], [side(1, zs), st + 1],
                 [side(0, zs), st + 1], [side(0, zs), zs], [xo[0], zx(0)]]);
      union() {   // plan: Dev's tab outline (round end, tapering to full width at the slope) above zt - 1
        box([xo[0], xo[1]], [lift_front - 1, lift_tab_y + 2], [b - 1, zt - 0.5]);
        translate([0, 0, zt - 1]) linear_extrude(st - zt + 2) hull() {
          translate([ax[0], ax[1]]) circle(r = lift_tab_r);
          translate([sx[0], riser_y - 1]) square([stop_w, yb - stop_t - riser_y + 2]);   // 1 past the slope's top edge
        }
      }
    }
    translate([ax[0], ax[1], zt - 1]) cylinder(d = lift_holes[0], h = tab_t + 2);
    for (o = [[-1, 0], [1, 0], [0, -1], [0, 1]] * lift_holes[2])
      translate([ax[0] + o[0], ax[1] + o[1], zt - 1]) cylinder(d = lift_holes[1], h = tab_t + 2);
  }
}
// Dev's nut position: flange against the tab's underside, spigot up in its hole, body hanging below
module lift_nut(top) translate([lift_axis[0], lift_axis[1], top + stop_h - tab_t]) {
  translate([0, 0, -nut_flange[1]]) cylinder(d = nut_flange[0], h = nut_flange[1]);
  translate([0, 0, -(nut_len - nut_spigot)]) cylinder(d = nut_d, h = nut_len);
}
