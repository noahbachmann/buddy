// Dispenser roller: shared geometry, built in dispenser.stl's frame (params.scad).
// Part files include this and turn their piece into print orientation.
include <BOSL2/std.scad>
include <BOSL2/gears.scad>
include <BOSL2/threading.scad>
include <params.scad>

eps = 0.01;
bed_bot = bed_top - bed_t;
axle_xs = [idler_x, drive_x];
drum_len = drum_y[0] - drum_y[1];
// pins (#57): back one in the plate's blind hole, front one through the front plate from outside
back_pin_y  = [back_y[0] + plate_t - 1 - pin[1], back_y[0] + plate_t - 1];
front_pin_y = [front_y[0], front_y[0] + pin[1]];
// pin tips in the drum's frame (z = drum_y[0] - y, as drum_tf), and the pockets for them
back_tip  = drum_y[0] - back_pin_y[0];
front_tip = drum_y[0] - front_pin_y[1];
back_pocket  = back_tip + pin_pocket[1];
front_pocket = front_tip - pin_pocket[1];
assert(back_tip > bearing[2] && front_tip < drum_len - bearing[2], "pins must reach through the bearings");
assert(back_pocket + pin_pocket[0] / 2 < front_pocket, "pin pockets meet: drum not solid");
// motor hole skipped: it would cut into the drive pin's blind hole
motor_hole_offs = [[-1, -1], [1, -1], [-1, 1]] * motor_holes / 2;
plate_cy = [(back_y[0] + back_y[1]) / 2, (front_y[0] + front_y[1]) / 2];
floor_screws = [for (x = [20, 60], y = plate_cy) [x, y]];   // from below through the box floor into the plates
rib_bot = drum_z - cord_c + cord_s / 2 + 1.5;              // 1.5 above the belts' bottom run

// --- Helpers --------------------------------------------------------------------------
// Teardrop for a horizontal hole: point towards +Z (tip = 1) or -Z (tip = -1).
// flat: tip cut off at the circle's top (short bridge), where something sits right above the hole
module teardrop2d(d, tip = 0, flat = false) {
  if (tip == 0) circle(d = d);
  else rotate(tip < 0 ? 180 : 0) intersection() {
    hull() { circle(d = d); rotate(45) square(d / 2); }
    if (flat) translate([-d, -d]) square([2 * d, 1.5 * d]);
  }
}
// Hole along Z from z0 to z1 at (x, y), teardrop towards -Y: up when the case prints on its back.
module hole_z(x, y, d, z0, z1) translate([x, y, z0]) linear_extrude(z1 - z0) rotate(180) teardrop2d(d, 1);
// Hole along Y from y1 down to y0, centre (x, z).
module hole_y(x, z, d, y0, y1, tip = 0)
  translate([x, y1, z]) rotate([90, 0, 0]) linear_extrude(y1 - y0) teardrop2d(d, tip);
// Box from corner ranges.
module box(xr, yr, zr) translate([xr[0], yr[0], zr[0]]) cube([xr[1] - xr[0], yr[1] - yr[0], zr[1] - zr[0]]);
// Printed peg along +Z from z 0, chamfered tip (prints upward).
module peg_z(h, d = peg_d) {
  cylinder(d = d, h = h - peg_cham);
  translate([0, 0, h - peg_cham - eps]) cylinder(d1 = d, d2 = d - 2 * peg_cham, h = peg_cham + eps);
}

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
    seat = bearing[1] + press;
    pd = pin_pocket[0];
    for (z = [-eps, drum_len - bearing[2]])
      translate([0, 0, z]) cylinder(d = seat, h = bearing[2] + eps);
    // solid between the pins (#57). Lower end: 45° cone from the seat into the pin pocket, pointed roof
    translate([0, 0, bearing[2] - eps]) cylinder(d1 = seat, d2 = pd, h = (seat - pd) / 2 + eps);
    translate([0, 0, bearing[2]]) cylinder(d = pd, h = back_pocket - bearing[2]);
    translate([0, 0, back_pocket - eps]) cylinder(d1 = pd, d2 = 0, h = pd / 2);
    // upper end: pocket below the seat
    translate([0, 0, front_pocket]) cylinder(d = pd, h = drum_len - bearing[2] - front_pocket + eps);
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
    translate([0, 0, gear_z0]) d_bore(gear_w + hub[1]);
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
lip_r   = gear_mod * (drum_teeth + 2) / 2 + lip_clear;   // the drum gear's tips (they reach 0.6 above the belts)
lip_y   = [front_y[1] - 1, back_y[0] + 1];   // into both legs by 1
// the slot's bottom follows the lip down (the case cuts the divider); thinner cards would need the divider higher
assert(slot_bot <= div_low, str("slot bottom ", slot_bot, " above the divider's lower part (", div_low, "): card_t too small"));
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
  leg_z = bed_top + 0.5;   // over the frame plates (they slide in under the legs; room to raise the belts 0.4 when tuning)
  difference() {
    union() {
      // bar + wedge, 1 into the divider block above its underside, from the front leg into the back wall (the case
      // prints on its back: a free back end would overhang)
      translate([0, back_wall + 0.5, 0]) rotate([90, 0, 0]) linear_extrude(back_wall + 0.5 - front_y[0])
        polygon([[gate_in, bar_z], [dx, bar_z], [dx, bar_z + 0.3], [dx + 1, bar_z + 0.3], [dx + 1, div_top - 0.5],
                 [dx - 0.2, div_top - 0.5], [gate_in, gate_top]]);
      // front leg 0.2 short of the plate's back face: with slit_play + divider_t = 4 the funnel's edge would pass
      // exactly through its corner at the divider (non-manifold)
      for (yr = [back_y, [front_y[0], front_y[1] - 0.2]]) box([gate_in, dx], yr, [leg_z, bar_z + 1.5]);
      difference() {   // lip, into both legs above the plates
        translate([0, lip_y[1], 0]) rotate([90, 0, 0]) linear_extrude(lip_y[1] - lip_y[0])
          // 0.5 into the divider (the slot cut trims it there): its end face would meet the divider's on x = dx
          polygon(concat([[edge, lip_top], [dx + 0.5, lip_top], [dx + 0.5, z45 - (dx + 0.5 - x45)]], arc));
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

// M3 pilot from a foot's underside z0 up: 10 deep, 45° lead-in at the mouth (top of the print: the plates print upside down)
module pilot_hole(x, y, z0, d) translate([x, y, z0]) {
  translate([0, 0, -eps]) cylinder(d = d, h = 10);
  translate([0, 0, -1]) cylinder(d1 = d + 2 * (pilot_cham + 1), d2 = d, h = pilot_cham + 1);
}

// --- Roller: frame (front plate + bed + ribs) and back plate (holds the 4023), two parts. Both print upside down:
// plate top on the build plate. The back plate goes in first (motor and gear screwed on with nothing over them), the
// frame slides in from the front, its bed between the belt runs, ribs 0 and 1 into the back plate's pockets. ---
module plate_profile(top)   // left of step_x it stands on the table, right of it on the box floor
  polygon([[frame_x[0], 0], [step_x, 0], [step_x, floor_top], [frame_x[1], floor_top], [frame_x[1], top], [frame_x[0], top]]);
module xz_plate(yr, top = bed_top)
  translate([0, yr[1], 0]) rotate([90, 0, 0]) linear_extrude(yr[1] - yr[0]) plate_profile(top);
// gate peg sockets in a plate's top
module peg_sockets(y)
  translate([gate_x(), y, bed_top - peg[2] - 0.5]) linear_extrude(peg[2] + 1.5)
    square([peg[0] + 2 * peg_fit, peg[1] + 2 * peg_fit], center = true);
// rib i, y range: the right rib stops short of the gear pair, the others reach into the back plate
bed_end = back_y[0] - bed_gap;
tongue_end = back_y[0] + rib_tongue;
// tongues past the bed's end: full height down to bed_top (in print: from the build plate up, else they float),
// 0.01 wider than the rib each side so no cut face lies in a rib face
tongue_xs = [for (i = [0 : 1]) [rib_xs[i] - 0.01, rib_xs[i] + rib_t + 0.01]];
function rib_y(i) = [front_y[1] - 1, i == 2 ? gear_y[0] - 0.5 : tongue_end - 0.5];   // short of the tongue's end face
// x ranges between and beside the tongues, widened by m
function off_tongues(m) = [[frame_x[0] - 1, tongue_xs[0][0] - m], [tongue_xs[0][1] + m, tongue_xs[1][0] - m],
                           [tongue_xs[1][1] + m, frame_x[1] + 1]];
// the left gate's front peg: slot open to the back, through the bed: the frame slides in under the gate (pegged into
// the back plate), the peg runs in the slot and ends in the front plate's socket
module gate_slot() let(w = peg[0] + 2 * peg_fit)
  box([gate_x() - w / 2, gate_x() + w / 2], [plate_cy[1] - peg[1] / 2 - peg_fit, back_y[0]], [bed_top - peg[2] - 0.5, bed_top + 1]);

// Front plate + bed: one L profile (y-z) extruded along X, then cut. Unioning them leaves coplanar faces, and those
// pinch vertices in the float32 STL. Nothing of the bed over the drums and the drum gear along the whole way: the
// drums sit on the back pins while the frame slides in.
// 45° chamfer on the top edge at y over x range r: lifts the belts as the frame slides under them
module lead_in(r, y) translate([r[0], 0, 0]) rotate([90, 0, 90]) linear_extrude(r[1] - r[0])
  polygon([[y - bed_lead - 1, bed_top + 1], [y + 1, bed_top + 1], [y + 1, bed_top - bed_lead - 1]]);

module frame() {
  w = rim_r + open_clear;
  gr = gear_mod * (drum_teeth + 2) / 2 + open_clear;
  cz = [bed_bot - 1, bed_top + 1];
  cy = front_y[1] - 0.01;   // 0.01 into the plate: a cut flush with its back face pinches vertices too
  difference() {
    union() {
      translate([frame_x[0], 0, 0]) rotate([90, 0, 90]) linear_extrude(frame_x[1] - frame_x[0])
        polygon([[front_y[0], 0], [front_y[1], 0], [front_y[1], bed_bot], [tongue_end, bed_bot], [tongue_end, bed_top],
                 [front_y[0], bed_top]]);
      for (i = [0 : 2]) box([rib_xs[i], rib_xs[i] + rib_t], rib_y(i), [rib_bot, bed_bot + 1]);
      // bosses: touch only the bearings' inner races
      for (x = axle_xs) translate([x, front_y[1] - eps, drum_z]) rotate([-90, 0, 0]) cylinder(d = boss_d, h = boss + eps);
    }
    // the plate's foot right of step_x stands on the box floor
    box([step_x, frame_x[1] + 1], [front_y[0] - 1, front_y[1] + 1], [-1, floor_top]);
    box([drive_x - w, frame_x[1] + 1], [cy, bed_end + 1], cz);
    box([drive_x - gr, frame_x[1] + 1], [gear_y[0] - 0.5, bed_end + 1], cz);
    box([frame_x[0] - 1, idler_x + w], [cy, bed_end + 1], cz);
    box(cam_window[0], cam_window[1], cz);
    // the bed ends at bed_end, only the tongues go on
    for (r = off_tongues(0)) box(r, [bed_end, tongue_end + 1], cz);
    // lead-ins under the belts: the bed's back edge beside the tongues, the tongues' tips
    for (r = off_tongues(0.02)) lead_in(r, bed_end);
    lead_in([frame_x[0] - 1, frame_x[1] + 1], tongue_end);
    // front pins: through, pushed in from the front into the bearings
    for (x = axle_xs) hole_y(x, drum_z, pin_hole, front_y[0] - eps, front_y[1] + boss + eps, -1);
    // screwdriver access to the motor screws
    for (o = motor_hole_offs) hole_y(motor_x + o[0], motor_z + o[1], 7, front_y[0] - eps, front_y[1] + eps, -1);
    gate_slot();
    for (p = floor_screws) if (p[1] < -90) pilot_hole(p[0], p[1], floor_top, screw_pilot);
  }
}

module back_plate() {
  difference() {
    union() {
      xz_plate(back_y);
      for (x = axle_xs) translate([x, back_y[0] + eps, drum_z]) rotate([90, 0, 0]) cylinder(d = boss_d, h = boss + eps);
    }
    // pins: blind
    for (x = axle_xs) hole_y(x, drum_z, pin_hole, back_y[0] - boss - eps, back_pin_y[1], -1);
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
    // pockets for the rib tongues
    for (t = tongue_xs) box([t[0] - tongue_fit, t[1] + tongue_fit], [back_y[0] - eps, back_y[0] + rib_tongue + 0.3],
                          [rib_bot - tongue_fit, bed_top + 1]);   // open at the top: the tongues run full height
    for (p = floor_screws) if (p[1] > -90) pilot_hole(p[0], p[1], floor_top, screw_pilot);
  }
}

// --- Bought parts (assembly and the combined STL only) -------------------------------------------
module pins() {
  for (x = axle_xs, r = [back_pin_y, front_pin_y]) hole_y(x, drum_z, pin[0], r[0], r[1]);
}
module belts() for (y = cord_ys) translate([0, y + cord_s / 2, 0]) rotate([90, 0, 0])
  linear_extrude(cord_s) difference() {
    hull() for (x = axle_xs) translate([x, drum_z]) circle(r = cord_out);
    hull() for (x = axle_xs) translate([x, drum_z]) circle(r = groove_root);
  }
module motor_4023() motor_tf() translate([-35, 25, -28.4]) import("ref/m4023.stl");

// === Pusher ======================================================================================
// Hole along X from x0 to x1, centre (y, z); tip = 1: teardrop pointing up (+Z), front: to -Y instead (the case's
// print direction).
module hole_x(y, z, d, x0, x1, tip = 0, front = false, flat = false)
  translate([x0, y, z]) rotate([0, 90, 0]) linear_extrude(x1 - x0) rotate(front ? 180 : 90) teardrop2d(d, front ? 1 : tip, flat);

// Plate side profile (x, z): chamfered lower edge, sharp-ish on the push side to catch the bottom card
function pu_profile(h = push_h) = let(b = push_lift, t = push_lift + h, x0 = right_wall[0], x1 = right_wall[0] + push_t,
                            c = push_cham)
  [[x0, b + c[0]], [x0 + c[0], b], [x1 - c[1], b], [x1, b + c[1]], [x1, t], [x0, t]];

// Guide ridge from x0 to x1 on a top at div_top (base 0.5 inside it): front face vertical (contact), flat top, back
// 45°; the front face set back at both ends over `taper`, so the groove slides over the step at a joint.
function guide_yb() = guide_yf + guide_top + guide_h;   // back face's foot at div_top
module guide_ridge(x0, x1, taper = guide_taper[1]) let(yf = guide_yf, z0 = div_top - 0.5, zt = div_top + guide_h,
                                                       yb = guide_yb() + 0.5, d = guide_taper[0])
  intersection() {
    translate([x0, 0, 0]) rotate([90, 0, 90]) linear_extrude(x1 - x0)
      polygon([[yf, z0], [yf, zt], [yf + guide_top, zt], [yb, z0]]);
    translate([0, 0, z0 - 1]) linear_extrude(zt - z0 + 2)
      polygon([[x0, yf + d], [x0 + taper, yf], [x1 - taper, yf], [x1, yf + d], [x1, yb + 1], [x0, yb + 1]]);
  }

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
    // round tab like the flange: the bar's width leaves no room for the nut's side pegs
    hole_x(pu_y, pu_screw_z, nut_flange[0], tab_x[0], tab_x[1]);
    box([right_wall[0] + 1, right_wall[0] + (push_t - 1)], [push_y[0] - 1, push_y[0] + 1], [b + 2, b + (bar_h - 1)]);  // joint
  }
  // hollow between `thin` legs (front leg = the groove's contact face), from the tab to a `thin` end wall at the plate:
  // under a `thin` deck (the guide ridge runs in it, the roof prints as a bridge), open above it in the gusset
  hy = [guide_yf - groove_play[0], push_y[0] - thin];
  assert(hy[1] - guide_yb() >= groove_play[1], "bar's back leg on the guide ridge");
  box([tab_x[1], right_wall[0]], hy, [b - 1, b + (bar_h - thin)]);
  box([bar_x[0] - 1, bar_x[1] + 1], [hy[0], guide_yb() + groove_play[1]], [b - 1, b + groove_d]);   // groove under tab, end
  box([tab_x[1], tab_x[1] + 16], hy, [b + bar_h, b + (bar_h + 16)]);
  // nut through the tab, flange on its right face (pushing presses the flange onto the tab), its 4 pegs pressed into
  // the tab like the lift's (they also carry the light return stroke and hold the nut square)
  hole_x(pu_y, pu_screw_z, nut_d + 0.4, tab_x[0] - eps, tab_x[1] + eps, 1, flat = true);   // full tip would cut into the upper peg hole
  ph = peg_d + 2 * fit_press_h;
  assert(nut_pcd / 2 - ph / 2 - (nut_d + 0.4) / 2 >= 0.7, "pusher tab: too little between the nut hole and a peg hole");
  assert(nut_flange[0] / 2 - nut_pcd / 2 - ph / 2 >= 1.2, "pusher tab: too little outside a peg hole");
  for (o = [[-1, 0], [1, 0], [0, -1], [0, 1]] * nut_pcd / 2)
    hole_x(pu_y + o[0], pu_screw_z + o[1], ph, tab_x[0] - eps, tab_x[1] + eps, 1, flat = true);
}

// Dev's change to the box: the right wall extended forward around the motor face (45° underneath, prints upright),
// the boss's hole and the motor's 4 screw holes through it.
m44_hole_offs = [for (a = [-1, 1], b = [-1, 1]) [a, b] * m44_holes / 2];
module case_add() difference() {   // additions to Dev's box: wall extension, slot ends, motor holds, borders, right gate, feet,
                                   // guide ridge on the divider
  dy = ext_y[1] - ext_y[0];
  union() {
    hull() {
      box([right_wall[0], m44_face], [ext_y[0], ext_y[1] + 1], ext_z);
      box([right_wall[0], m44_face], [ext_y[1], ext_y[1] + 1], [ext_z[0] - dy, ext_z[0]]);
    }
    slot_ends();
    roller_hold();
    pusher_hold();
    h_back_border();
    right_gate();
    feet();
    guide_ridge(divider_x + 0.1, slit_x1 - 0.1, 1.2);   // on the divider's top, between the borders' ridges
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
module feet() for (x = [0, right_wall[0]]) box([x, x + feet_t], [box_front, back_out], [lift_face - feet_h, floor_bot + 0.5]);

// Dev's cuts in the box (Onshape), here for the preview and the combined STL.
// 4023: the hold carries the motor from below, from the frame's back plate through the wall to the motor's back end
m23_box = [[motor_x - motor_sq / 2, motor_x + motor_sq / 2], [motor_face_y, motor_face_y + motor_body],
           [motor_z - motor_sq / 2, motor_z + motor_sq / 2]];
// It ends at the wall's outer face (the case prints on it): the motor's last 3 mm overhang.
module roller_hold() box(m23_box[0], [m23_box[1][0] + hold_gap, min(m23_box[1][1], back_out)], [floor_bot, m23_box[2][0]]);
// 17HS4401 on the right wall: a wedge under its body, 45° underside, and 45° along the wall towards the back (the case
// prints on its back, -Y up: a flat back face would overhang)
module pusher_hold() {
  top = pu_screw_z - m44_sq / 2;
  x0 = m44_face - 1;   // into the wall
  x1 = m44_face + m44_body;
  y1 = pu_y + pu_hold[0] / 2;
  hull() {
    translate([0, y1, 0]) rotate([90, 0, 0]) linear_extrude(pu_hold[0])
      polygon([[x0, top - (x1 - x0) - pu_hold[1]], [x0, top], [x1, top], [x1, top - pu_hold[1]]]);
    box([x0, x0 + 0.01], [y1, y1 + (x1 - x0)], [top - (x1 - x0) - pu_hold[1], top]);
  }
}
// Pile H's borders: left wall .. divider, from just under the belt top to the divider top: no card leaves H to the
// front or back. Inner faces 0.5 off the cards at the bottom, leaning out like the lift's slope.
h_bord_z = [belt_top - h_bord_drop, div_top];
h_lean   = (h_bord_z[1] - h_bord_z[0]) * stop_lean / stop_h;
// what passes under them: the frame's bed (slides in from the left), the drive drum's rim (slides in from the front;
// its gear gets a channel), the 4023's top (in from behind, through the back wall's window)
assert(h_bord_z[0] - (drum_z + rim_r) >= 0.3 && h_bord_z[0] - (motor_z + motor_sq / 2) >= 0.3
       && h_bord_z[0] - bed_top >= 0.5, "pile H's borders: too low over drum, 4023 or bed");
h_wall_z = left_wall_bottom + 0.5;   // above it the back border reaches into the left wall and through the right gate
// Back: part of the case, solid back to the wall: the case prints on its back, a free outer face would overhang.
// Below h_wall_z only between the gates: the left gate (x < 0) sits there, and right of gate_in is the funnel's mouth.
module h_back_border() let(zb = h_bord_z[0], zt = h_bord_z[1], y1 = card_yc + card[1] / 2 + 0.5)
  difference() {
    translate([left_wall[1] - 1, 0, 0]) rotate([90, 0, 90]) linear_extrude(divider_x - (left_wall[1] - 1))
      polygon([[y1, zb], [y1 + h_lean, zt], [back_wall + 0.5, zt], [back_wall + 0.5, zb]]);
    for (x = [[left_wall[1] - 2, left_wall[1] + insert_fit], [gate_in, divider_x + 1]])
      box(x, [y1 - 1, back_wall + 1], [zb - 1, h_wall_z]);
    // the drive drum's gear slides in from the front and runs under it (as the right gate's lip)
    translate([drive_x, drum_y[0] + 2, drum_z]) rotate([90, 0, 0]) cylinder(r = lip_r, h = drum_y[0] + 2 - (y1 - 1));
  }

// Front borders of both piles: slide-in parts (their card faces would overhang with the case on its back). Each fills
// the front strip in front of its pile, flush with the box front. Profiles (y, z), extruded along X.
// H: left wall .. the right gate's wedge; the step beside the gate's front face is its stop, 2 printed pins through
// the left wall.
function h_insert_profile() = let(zb = h_bord_z[0], zt = h_bord_z[1], y1 = card_yc - card[1] / 2 - 0.5)
  [[box_front, zb], [y1, zb], [y1 - h_lean, zt], [box_front, zt]];
module h_insert() {
  f = insert_fit;
  difference() {
    union() {
      translate([left_wall[1] + f, 0, 0]) rotate([90, 0, 90]) linear_extrude(divider_x - f - (left_wall[1] + f))
        polygon(h_insert_profile());
      guide_ridge(left_wall[1] + f, divider_x - f);
    }
    // the right gate's bar and wedge (inner face from (gate_in, gate_top) to (divider_x - 0.2, div_top - 0.5)), + fit
    translate([0, back_wall, 0]) rotate([90, 0, 0]) linear_extrude(back_wall - (front_y[0] - f))
      polygon([[gate_in - f, h_bord_z[0] - 1], [gate_in - f, gate_top], [divider_x - 0.2 - f, div_top - 0.5],
               [divider_x - 0.2 - f, div_top + 1], [divider_x + 1, div_top + 1], [divider_x + 1, h_bord_z[0] - 1]]);
    // border pins through the left wall (vertical in print: the border stands on its left end)
    for (s = insert_screws_h) hole_x(s[0], s[1], peg_d + 2 * fit_press, left_wall[1], left_wall[1] + bpin_depth + 1);
    // border pins through the divider (vertical in print), shank tip + 1
    for (s = hplug_yz) hole_x(s[0], s[1], peg_d + 2 * fit_press, hplug_tip - 1, divider_x + 1);
  }
}
// B: divider .. right wall, on the floor; a lip over the floor's front edge is its stop, 2 printed pins from below.
// Inner face vertical 0.5 in front of the lift plate up to its lowest top, then leaning forward like the slope up to
// div_low, then vertical up to the divider's top: it carries the pusher's guide ridge like border H.
function b_insert_profile() = let(y1 = lift_front - 0.5, lean = (div_low - lift_min) * stop_lean / stop_h,
                                  bf = box_front)
  [[bf - 2, floor_bot + 0.5], [bf, floor_bot + 0.5], [bf, floor_top], [y1, floor_top], [y1, lift_min], [y1 - lean, div_low], [y1 - lean, div_top], [bf, div_top],
   [bf, floor_top + 5], [bf - 2, floor_top + 5]];
module b_insert() {
  f = insert_fit;
  difference() {
    union() {
      translate([slit_x1 + f, 0, 0]) rotate([90, 0, 90]) linear_extrude(right_wall[0] - f - (slit_x1 + f))
        polygon(b_insert_profile());
      guide_ridge(slit_x1 + f, right_wall[0] - f);
    }
    // border pins from below (horizontal in print: the border stands on its divider end, +X up)
    for (s = insert_screws_b) translate([s[0], s[1], floor_top - eps]) linear_extrude(bpin_depth + 1)
      rotate(-90) teardrop2d(peg_d + 2 * bpin_up_fit, 1, flat = true);
    // border pins through the right wall (vertical in print)
    for (s = insert_pins_br) hole_x(s[0], s[1], peg_d + 2 * fit_press, right_wall[0] - bpin_depth - 1, right_wall[0]);
  }
}
// Border pin from pile B's side, head seated in the divider's counterbore: where its shank tip lands in border H
hplug_tip = slit_x1 - hplug_cb[1] - (wall_t + bpin_depth - 0.5);
assert(divider_x - insert_fit - hplug_tip >= 6, "border H: too little of the divider's pins in it");
// Dev's box as drawn, its front and top cut back, walls and floor thinned to wall_t, divider to divider_t
// Divider: the part right of it moves left by b_shift. The right piece starts 0.5 inside the divider: the two overlap
// by 0.5 with the same section (floor, back wall, divider), no touching faces.
module box_dev() {
  intersection() { import("ref/box.stl"); box([left_wall[0] - 1, slit_x1], [-200, 10], [-1, 201]); }
  translate([-b_shift, 0, 0]) intersection() {
    import("ref/box.stl");
    box([divider_x1_dev - 0.5, 160], [-200, 10], [-1, 201]);
  }
}
// The notch's skin joins before the front cut: one cut makes its front face and the wall's (added later, flush, the
// two faces miss by an ulp and the STL isn't watertight).
module box_trimmed() difference() {
  union() {
    difference() { box_dev(); wall_thinning(); }
    notch_skin();
  }
  box([left_wall[0] - 1, right_wall[1] + 1], [box_front - 20, box_front], [-1, 201]);
  box([left_wall[0] - 1, right_wall[1] + 1], [box_front - 1, 1], [box_top, 201]);
}
// From the outside: floor from below, back wall from behind, the side walls from their outer faces: all 3 thick, no pads
// (the pusher's notch gets its own skin: notch_skin)
module wall_thinning() {
  xs = [left_wall[0] - 1, right_wall[1] + 1];
  ys = [box_front - 1, back_out + 1];
  box(xs, [box_front - 1, back_out + 2], [-1, floor_bot]);
  box(xs, [back_out, back_out + 3], [-1, 201]);
  box([left_wall[0] - 1, left_wall[1] - wall_t], ys, [-1, 201]);
  box([right_wall[0] + wall_t, right_wall[1] + 1], ys, [-1, 201]);
}
// Skin behind the pusher's notch, which goes through the 3 mm right wall: notch_skin behind it and around it, the
// pusher motor sitting on its top (same z as the hold's top). Back end 45° (the case prints on its back, -Y up).
module notch_skin() let(x0 = m44_face - 1, x1 = m44_face + notch_skin, y1 = riser_y + notch_skin,
                        z0 = notch_lo[1][0] - notch_skin, z1 = pu_screw_z - m44_sq / 2)
  translate([0, 0, z0]) linear_extrude(z1 - z0)
    polygon([[x0, box_front - 1], [x1, box_front - 1], [x1, y1], [m44_face, y1 + notch_skin], [x0, y1 + notch_skin]]);

module box_cuts() {
  // 4023: window through the back wall, its floor = the hold's top; channel for the connector and cable, out below
  box([m23_box[0][0] - m23_window, m23_box[0][1] + m23_window], [back_wall - 1, 1],
      [m23_box[2][0], m23_box[2][1] + m23_window]);
  box([motor_x - cable_slot[0] / 2, motor_x + cable_slot[0] / 2],
      [m23_box[1][1] - m23_conn[2] - cable_slot[1], m23_box[1][1] + 1], [-1, m23_box[2][0] + 1]);
  for (n = [notch_lo, notch_hi]) box([right_wall[0] - 1, right_wall[0] + push_t], n[0], n[1]);
  wall_holes();
  hole_x(pu_y, pu_screw_z, screw_hole, left_wall[0] - 1, left_wall[1] + 1, front = true);
  // divider's lower part down to slot_bot inside the slit's funnel (its ends stay filled: slot_ends)
  translate([0, 0, slot_bot]) linear_extrude(div_low + 0.5 - slot_bot)
    polygon([for (s = [0, 1], x = s == 0 ? [divider_x, slit_x1 + 1] : [slit_x1 + 1, divider_x]) [x, slit_edge(s, x)]]);
  // lift motor under the floor: boss hole + the 2 front screw holes (the back 2 are under the wall)
  hole_z(lift_axis[0], lift_axis[1], m44_boss[0] + 0.6, -1, floor_top + 1);
  for (o = m44_hole_offs) if (o[1] < 0) hole_z(lift_axis[0] + o[0], lift_axis[1] + o[1], screw_clear, -1, floor_top + 1);
  // frame to the floor, border inserts
  for (p = floor_screws) hole_z(p[0], p[1], screw_clear, -1, floor_top + 1);
  for (s = insert_screws_b) hole_z(s[0], s[1], screw_clear, -1, floor_top + 1);
  for (s = insert_screws_h) hole_x(s[0], s[1], screw_clear, left_wall[0] - 1, left_wall[1] + 1, front = true);
  for (s = insert_pins_br) hole_x(s[0], s[1], screw_clear, right_wall[0] - 1, right_wall[1] + 1, front = true);
  for (s = hplug_yz) {   // border H's pins through the divider, head sunk from pile B's side
    hole_x(s[0], s[1], screw_clear, divider_x - 1, slit_x1 + 1, front = true);
    hole_x(s[0], s[1], hplug_cb[0], slit_x1 - hplug_cb[1], slit_x1 + 1, front = true);
  }
}
// The printed case: Dev's box with every change, as it sits (case.scad turns it onto its back)
module case() difference() { union() { box_trimmed(); case_add(); } box_cuts(); }

// Case spares: the same case minus what carries nothing (case-spares.scad). Still on its back, still no supports:
// back-wall windows lie on the bed; every other cut's roof (its -Y side) is 45° or vertical in print.
module case_spares() difference() { case(); spare_cuts(); }
module spare_cuts() {
  // rear top: behind the pusher motor nothing needs the walls above spare_top (cards end at push_lift + pile_max)
  translate([left_wall[0] - 1, 0, 0]) rotate([90, 0, 90]) linear_extrude(m44_face + 2 - (left_wall[0] - 1))
    polygon([[spare_y - 1, box_top + 1], [spare_y + box_top - spare_top, spare_top], [back_out + 1, spare_top],
             [back_out + 1, box_top + 1]]);
  // back wall windows (on the bed): pile H above its back border; pile B around a post behind the lift tab's path
  r = spare_rim;
  tab_x = [lift_axis[0] - lift_tab_r - spare_tab, lift_axis[0] + lift_tab_r + spare_tab];
  tab_z = lift_min + stop_h - tab_t - spare_tab;   // tab's underside at the lift's lowest
  b_x = [divider_x + divider_t + wall_t, right_wall[0] - r];
  // pile B: open below a crossbar under the post; beside the post X-braced slots
  translate([0, back_out + 1, 0]) rotate([90, 0, 0]) linear_extrude(back_out - back_wall + 2) {
    translate([r, div_top + r]) square([divider_x - wall_t - r, spare_top - r - (div_top + r)]);
    translate([b_x[0], floor_top + r]) square([b_x[1] - b_x[0], tab_z - r - (floor_top + r)]);
    for (sx = [[b_x[0], tab_x[0]], [tab_x[1], b_x[1]]]) difference() {
      translate([sx[0], tab_z]) square([sx[1] - sx[0], spare_top - r - tab_z]);
      x_lattice(sx, [tab_z, spare_top - r], spare_strut);
    }
  }
  // floor under the lift: pointed to the front (roof in print), flat at the back short of the lift motor's face
  fy = [lift_front + r, lift_axis[1] - m44_sq / 2 - wall_t];
  fx = [divider_x + divider_t + r, right_wall[0] - r];
  w2 = (fx[1] - fx[0]) / 2;
  translate([0, 0, floor_bot - 1]) linear_extrude(wall_t + 2)
    polygon([[fx[0], fy[1]], [fx[0], fy[0] + w2], [fx[0] + w2, fy[0]], [fx[1], fy[0] + w2], [fx[1], fy[1]]]);
  // feet: Warren truss, 45° diagonals, whole triangles only, centred
  for (x = [0, right_wall[0]]) translate([x - 1, 0, 0]) rotate([90, 0, 90]) linear_extrude(feet_t + 2) foot_windows();
}
// 2D: stacked X-braces over the rectangle xr × zr, cells about square, ties between them, struts s wide
module x_lattice(xr, zr, s) let(w = xr[1] - xr[0], n = max(1, round((zr[1] - zr[0]) / w)), h = (zr[1] - zr[0]) / n)
  for (i = [0:n - 1]) let(z0 = zr[0] + i * h) {
    for (d = [[[xr[0], z0], [xr[1], z0 + h]], [[xr[1], z0], [xr[0], z0 + h]]])
      hull() for (p = d) translate(p) square(s, center = true);
    if (i > 0) translate([xr[0], z0 - s / 2]) square([w, s]);
  }
module foot_windows() let(za = lift_face - feet_h + foot_chords[0], zb = floor_bot - foot_chords[1], h = zb - za,
                          ya = box_front + spare_rim, yb = back_out - spare_rim, n = floor((yb - ya) / h),
                          y0 = (ya + yb - n * h) / 2, p = [for (k = [0:n]) [y0 + k * h, k % 2 == 0 ? za : zb]])
  for (k = [0:n - 2]) offset(delta = -foot_strut / 2) polygon([p[k], p[k + 1], p[k + 2]]);

// Bought parts. 17HS4401 in its own frame: face at z 0, body below, shaft up; coupling on the shaft.
module m44_local() {
  translate([0, 0, -m44_body]) linear_extrude(m44_body) offset(r = 3) offset(delta = -3) square(m44_sq, center = true);
  translate([0, 0, -eps]) cylinder(d = m44_boss[0], h = m44_boss[1] + eps);
  cylinder(d = m44_shaft[0], h = m44_shaft[1]);
}
module coupling_local() translate([0, 0, m44_shaft[1] - plug_cpl[1] / 2]) plug_coupling();

// Plug coupling, local: axis Z, z 0 = motor end. Lower half: D-bore pressed onto the 17HS4401's shaft (flat at +X).
// Upper half: TR8x8 internal thread; the screw turns in until its end jams on the step / the shaft tip in the middle.
// Prints standing, motor end down (thread standing, as the nuts). The jam holds the screw only while the torque on it
// stays below the tightening torque: tighten hard by hand (pliers on the screw, motor held).
module plug_coupling() let(h = plug_cpl[1]) difference() {
  cylinder(d = plug_cpl[0], h = h, $fn = 96);
  d_bore(h / 2, plug_bore);
  translate([0, 0, h / 2]) tr8_thread(h / 2, plug_flank, bevel1 = false);
  translate([0, 0, -eps]) difference() {   // 0.5 chamfer on the outer bed edge: elephant foot
    cylinder(d = plug_cpl[0] + 1, h = 0.5 + eps, $fn = 96);
    cylinder(d1 = plug_cpl[0] - 1, d2 = plug_cpl[0], h = 0.5 + eps, $fn = 96);
  }
}

// Printed rigid coupling, local: axis Z, z 0 = motor end. Bores meet at the middle (screw end stops on the step).
// One slot along +Y; per end one M3x12 across it (along X): head pocket at +x, hex nut pocket at -x, both open to
// the outside. Prints standing, motor end down: bores round, horizontal holes teardrops (tip up).
module rigid_coupling() {
  h = coupling[1];
  d5 = 5 + 2 * cpl_gap;
  d8 = 8 + 2 * cpl_gap;
  module along_x(x0, x1) translate([x0, 0, 0]) rotate([90, 0, 90]) linear_extrude(x1 - x0) children();
  difference() {
    cylinder(d = coupling[0], h = h, $fn = 96);
    translate([0, 0, -eps]) cylinder(d = d5, h = h / 2 + 2 * eps, $fn = 48);
    translate([0, 0, h / 2]) cylinder(d = d8, h = h / 2 + eps, $fn = 64);
    translate([0, 0, -eps]) cylinder(d1 = d5 + 0.8, d2 = d5, h = 0.4, $fn = 48);          // elephant foot
    translate([0, 0, h - 0.6 + eps]) cylinder(d1 = d8, d2 = d8 + 1.2, h = 0.6, $fn = 64);  // lead-in
    translate([-cpl_slot / 2, 0, -eps]) cube([cpl_slot, coupling[0], h + 2 * eps]);
    for (z = [h / 4, 3 * h / 4]) translate([0, cpl_bolt[0], z]) {
      along_x(-coupling[0], coupling[0]) teardrop2d(screw_clear, 1);
      along_x(cpl_bolt[1], coupling[0]) teardrop2d(6.2, 1);                              // head Ø5.5
      along_x(-coupling[0], -cpl_bolt[1]) let(r = 5.8 / sqrt(3))                            // nut 5.5 AF, 45° roof
        hull() { rotate(90) circle(r = r, $fn = 6); translate([0, r / 2 + 2.9]) square(0.01, center = true); }
    }
  }
}
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
module lift_screw() lift_tf() translate([0, 0, m44_shaft[1]]) cylinder(d = 8, h = lift_screw_top - lift_face - m44_shaft[1]);

// --- Lift (printed): plate + slope + Dev's nut tab. Plate top at `top`. Print: as placed, plate down; the tab (Dev's
// design) needs support: Bambu Studio's auto supports. One intersection, no unions: side profile (along X) ∩ front
// profile (along Y: straight sides from the plate's corners to the tab's) ∩ plan (along Z: everything up to just
// under the tab, the tab's outline above).
assert(card_yc + card[1] / 2 <= gear_y[0] - 1, "cards reach the drum gear");
assert(lift_axis[1] - nut_flange[0] / 2 - (riser_y + stop_t + stop_lean * (stop_h - tab_t) / stop_h) >= 0.5 - 1e-9,
       "nut flange hits the slope");
module lift_part(top) {
  b = top - lift_t;
  st = top + stop_h;
  zt = st - tab_t;                       // tab underside = nut flange top
  yb = function(z) riser_y + stop_t + stop_lean * (z - top) / stop_h;   // slope wall's back face, parallel to its front
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
        polygon([[lift_front, b], [yb(b), b], [yb(zt), zt], [lift_tab_y + 1, zt], [lift_tab_y + 1, st], [riser_y + stop_lean, st],
                 [riser_y, top], [lift_front, top]]);
      translate([0, lift_tab_y + 2, 0]) rotate([90, 0, 0]) linear_extrude(lift_tab_y + 2 - (lift_front - 1))
        polygon([[xo[0], b - 1], [xo[1], b - 1], [xo[1], zx(1)], [side(1, zs), zs], [side(1, zs), st + 1],
                 [side(0, zs), st + 1], [side(0, zs), zs], [xo[0], zx(0)]]);
      union() {   // plan: Dev's tab outline (round end, tapering to full width at the slope) above zt - 1
        box([xo[0], xo[1]], [lift_front - 1, lift_tab_y + 2], [b - 1, zt - 0.5]);
        translate([0, 0, zt - 1]) linear_extrude(st - zt + 2) hull() {
          translate([ax[0], ax[1]]) circle(r = lift_tab_r);
          translate([sx[0], riser_y - 1]) square([stop_w, stop_lean + 2]);   // 1 past the slope's top edge
        }
      }
    }
    translate([ax[0], ax[1], zt - 1]) cylinder(d = lift_holes[0], h = tab_t + 2);
    for (o = [[-1, 0], [1, 0], [0, -1], [0, 1]] * lift_holes[1])   // the nut's pegs, pressed in
      translate([ax[0] + o[0], ax[1] + o[1], zt - 1]) cylinder(d = peg_d + 2 * fit_press, h = tab_t + 2);
  }
}
// flange against the tab's underside, body hanging below (the tab's Ø10.2 hole stays empty: the screw centres the nut)
// Lift nut in place: flange top against the tab's underside, pegs up in the tab, body down (the screw pushes it up into
// the tab, the lift's weight keeps it there)
module lift_nut(top) translate([lift_axis[0], lift_axis[1], top + stop_h - tab_t - nut_len]) lift_nut_outside();

// --- TR8x8 nuts, printed (#63: pegs instead of screws, a pair per nut type) -----------------------------------
// Pusher nut: flange at z 0..nut_flange[1] on the bed, body up; 4 pegs on the flange's top face go into the tab.
// Lift nut: body on the bed, 45° cone up to the flange at the top, 4 pegs up into the tab. Axis = Z for both.
// Outside only (stand-ins for the checks: no thread, no pegs)
module nut_outside() {
  cylinder(d = nut_flange[0], h = nut_flange[1]);
  cylinder(d = nut_d, h = nut_len);
}
lift_cone = (nut_flange[0] - nut_d) / 2;   // 45°: prints without support
module lift_nut_outside() {
  cylinder(d = nut_d, h = nut_len);
  translate([0, 0, nut_len - nut_flange[1] - lift_cone]) cylinder(d1 = nut_d, d2 = nut_flange[0], h = lift_cone + eps);
  translate([0, 0, nut_len - nut_flange[1]]) cylinder(d = nut_flange[0], h = nut_flange[1]);
}
// internal thread: BOSL2 adds 2 $slop to the radius. Radial gap = flank gap / sin 15°; bevel = lead-in
module tr8_thread(len = nut_len, flank = nut_flank, bevel1 = true) translate([0, 0, len / 2]) let($slop = flank / sin(15) / 2)
  trapezoidal_threaded_rod(d = 8, l = len + 2 * eps, pitch = lead / nut_starts, thread_angle = 30,
                           starts = nut_starts, internal = true, bevel1 = bevel1, bevel2 = true, $fn = 64);
// D-bore for the 4023's 5 mm shaft (flat 2.0 from the axis), bore_gap per side, from z 0 up; 0.4 chamfer at z 0
// against elephant foot (bed side)
module d_bore(h, gap = bore_gap) {
  module d(o) intersection() { circle(d = 5 + 2 * gap + 2 * o); translate([-5, -5]) square([5 + 2 + gap + o, 10]); }
  translate([0, 0, -eps]) linear_extrude(h + 2 * eps) d(0);
  translate([0, 0, -eps]) hull() { linear_extrude(eps) d(0.4); translate([0, 0, 0.4]) linear_extrude(eps) d(0); }
}
module pusher_nut() difference() {
  union() {
    nut_outside();
    for (a = [0 : 90 : 270]) rotate(a) translate([nut_pcd / 2, 0, nut_flange[1] - eps]) peg_z(nut_peg_h + eps);
  }
  tr8_thread();
}
module lift_nut_part() difference() {
  union() {
    lift_nut_outside();
    for (a = [0 : 90 : 270]) rotate(a) translate([nut_pcd / 2, 0, nut_len - eps]) peg_z(nut_peg_h + eps);
  }
  tr8_thread();
}

// --- Border pin: replaces an M3 screw; through the case's Ø3.4 hole into the border, pressed. Head down. ---------
module border_pin() {
  cylinder(d = bpin_head[0], h = bpin_head[1]);
  translate([0, 0, bpin_head[1] - eps]) peg_z(wall_t + bpin_depth - 0.5 + eps);
}

// --- Fit test (plate 0): the hole steps every printed joint uses ------------------------------------------------
// Base 3 thick (the tabs): row of vertical peg holes; block 6 thick (the front plate): holes for the Ø5 steel pin;
// standing 3 mm wall: horizontal teardrop peg holes. Step 1 at the chamfered corner (x 0). Test with a border pin.
module fit_test() {
  n = len(fit_steps);
  p = 9;
  w = n * p + 2;
  difference() {
    union() {
      box([0, w], [0, 9], [0, 3]);              // vertical peg holes
      box([0, w], [9, 19], [0, plate_t]);       // steel pin holes
      box([0, w], [19, 22], [0, 12]);           // horizontal peg holes (wall)
    }
    for (i = [0 : n - 1]) {
      x = 1 + p / 2 + i * p;
      translate([x, 4.5, -1]) cylinder(d = peg_d + 2 * fit_steps[i], h = 5);
      translate([x, 14, -1]) cylinder(d = pin_steps[i], h = plate_t + 2);
      translate([x, 18, 9]) rotate([-90, 0, 0]) linear_extrude(5) rotate(180) teardrop2d(peg_d + 2 * fit_steps[i], 1, flat = true);   // tip up
    }
    // marker: chamfered corner at step 1
    translate([0, 0, -1]) linear_extrude(14) polygon([[-1, -1], [4, -1], [-1, 4]]);
  }
}

// --- Pilot test (plate 0c): M3 pilot holes at pilot_steps in a 6 mm wall like the roller plates' feet ----------
// Printed as the feet are: mouth at the top (feet print last, the plates are upside down), 10 deep, same lead-in.
// Hole 1 at the chamfered corner.
module pilot_test() {
  n = len(pilot_steps);
  p = 8;
  h = 13;
  difference() {
    box([0, n * p + 2], [0, plate_t], [0, h]);
    for (i = [0 : n - 1]) translate([1 + p / 2 + i * p, plate_t / 2, h]) mirror([0, 0, 1]) pilot_hole(0, 0, 0, pilot_steps[i]);
    translate([0, 0, -1]) linear_extrude(h + 2) polygon([[-1, -1], [3, -1], [-1, 3]]);
  }
}

// --- Thread test (plate 0b): TR8 nut rings at nut_flank_steps, D-bore rings at bore_steps -------------------------
// Ring i has i ribs on its outside. Print at 0.12 like plate 1.
module thread_test() {
  ring_h = 8;
  module ribs(n, r, h) for (k = [0 : n - 1]) rotate(k * 25) translate([r - 0.5, -0.6, 0]) cube([1.5, 1.2, h]);
  for (i = [0 : len(nut_flank_steps) - 1]) translate([i * 18, 0, 0]) difference() {
    union() { cylinder(d = 14, h = ring_h, $fn = 64); ribs(i + 1, 7, ring_h); }
    tr8_thread(ring_h, nut_flank_steps[i]);
  }
  for (i = [0 : len(bore_steps) - 1]) translate([i * 16, 20, 0]) difference() {
    union() { cylinder(d = 11, h = 6, $fn = 48); ribs(i + 1, 5.5, 6); }
    d_bore(6, bore_steps[i]);
  }
}
