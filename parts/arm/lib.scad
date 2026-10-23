// Buddy arm: shared geometry. Part files include this and pick their piece.
include <params.scad>

big = 500;

// --- Profiles -----------------------------------------------------------------
function seg(p, z) = [z, lookup(z, [for (e = p) [e[0], e[1]]]),
                         lookup(z, [for (e = p) [e[0], e[2]]])];
function clip(p, z0, z1) = concat([seg(p, z0)], [for (e = p) if (e[0] > z0 && e[0] < z1) e], [seg(p, z1)]);

module ellipse(a, b) scale([a, b]) circle(1);
module slab(z, h = 0.01) translate([0, 0, z]) linear_extrude(h) children();
module ring_slice(e, inset = 0) slab(e[0]) translate([0, yc]) ellipse(e[1] - inset, e[2] - inset);
module rect_slice(z, hx, y0, y1) slab(z) translate([-hx, y0]) square([2 * hx, y1 - y0]);
module loft(p, inset = 0)
  for (i = [0 : len(p) - 2]) hull() { ring_slice(p[i], inset); ring_slice(p[i + 1], inset); }

// --- Holes ----------------------------------------------------------------------
// Teardrop: printable horizontal hole, point towards +Z (tip = 1) or -Z (tip = -1).
module teardrop2d(d, tip = 0) {
  if (tip == 0) circle(d = d);
  else rotate(tip < 0 ? 180 : 0) hull() { circle(d = d); rotate(45) square(d / 2); }
}
module hole_y(d, y0, y1, tip = 0)
  translate([0, y1, 0]) rotate([90, 0, 0]) linear_extrude(y1 - y0) teardrop2d(d, tip);
module hole_x(d, x0, x1, tip = 0)
  translate([x0, 0, 0]) rotate([0, 0, 90]) rotate([90, 0, 0]) linear_extrude(x1 - x0) teardrop2d(d, tip);

// --- Knuckle: houses a servo, shaft on the pivot, limb towards +Z ------------------
// The servo slides in from +Y; its flange sits on the +Y face.
// Idler side (-Y): boss that clamps the 608's inner race, M8 bolt into a captive nut.
module knuckle(len) {
  translate([0, y_flange, 0]) rotate([90, 0, 0]) linear_extrude(knuckle_w) {
    circle(knuckle_r);
    translate([-stem_half, 0]) square([2 * stem_half, len]);
  }
  translate([0, y_back, 0]) rotate([90, 0, 0]) cylinder(d = boss_d, h = boss_h);
}

// channel_to: how far up the cable channel runs. Knuckles print pivot-up (tip = -1).
module knuckle_cuts(channel_to) {
  hw = servo_body[1] / 2 + fit;
  z0 = -servo_shaft_off - fit;
  z1 = servo_body[0] - servo_shaft_off + fit;
  // servo body, open towards +Y
  translate([-hw, pocket_y0, z0]) cube([2 * hw, big, z1 - z0]);
  // cable groove along one side, in case the cable exits at the shaft end
  translate([hw - 0.01, pocket_y0, z0]) cube([2, 4, z1 - z0]);
  // cable channel up into the limb
  translate([-hw, pocket_y0, z1 - 1]) cube([2 * hw, channel_h, channel_to - z1 + 1]);
  // flange screws
  for (z = [body_c - servo_hole_pitch[0] / 2, body_c + servo_hole_pitch[0] / 2],
       x = [-servo_hole_pitch[1] / 2, servo_hole_pitch[1] / 2])
    translate([x, y_flange + 0.01, z]) rotate([90, 0, 0]) cylinder(d = servo_screw_d, h = 12);
  // M8 axle bolt + captive nut, slot opens to the front (+X), hex point up for printing
  hole_y(bolt_d, y_back - boss_h - 0.01, y_back + outer_wall + nut_slot_t, -1);
  translate([0, y_back + outer_wall + nut_slot_t, 0]) rotate([90, 0, 0])
    linear_extrude(nut_slot_t) hull()
      for (x = [0, knuckle_r + 2]) translate([x, 0]) rotate(90) circle(d = (nut_af + 0.4) / cos(30), $fn = 6);
}

// --- Fork: grips a knuckle, pivot at origin, limb towards -Z ------------------------
// +Y cheek bolts to the servo horn, -Y cheek carries the 608 bearing.
module cheek_shape()
  hull() {
    circle(cheek_r);
    translate([-fork_half_x, -fork_bridge_z - fork_bridge_t]) square([2 * fork_half_x, fork_bridge_t]);
  }

module fork() {
  for (c = [[fork_y0, idler_cheek_t], [horn_cheek_y, horn_cheek_t]])
    translate([0, c[0] + c[1], 0]) rotate([90, 0, 0]) linear_extrude(c[1]) cheek_shape();
  translate([-fork_half_x, fork_y0, -fork_bridge_z - fork_bridge_t])
    cube([2 * fork_half_x, fork_y1 - fork_y0, fork_bridge_t]);
}

// Forks print pivot-up (tip = +1).
module fork_cuts() {
  // horn cheek: screwdriver access to the horn screw, 4x M3 counterbored
  hole_y(8, horn_cheek_y - 1, fork_y1 + 1);
  for (a = [45 : 90 : 315]) translate([horn_pcd / 2 * cos(a), 0, horn_pcd / 2 * sin(a)]) {
    hole_y(3.4, horn_cheek_y - 1, fork_y1 + 1);
    hole_y(6.5, fork_y1 - 3.2, fork_y1 + 1);
  }
  // idler cheek: bearing seat from the inside, lip outside
  hole_y(bearing[1] + press, idler_in - bearing_recess - bearing[2], idler_in + 1, 1);
  hole_y(lip_hole_d, fork_y0 - 1, idler_in, 1);
  // groove that lets the knuckle's boss slide in from the top
  translate([0, idler_in + 0.01, 0]) rotate([90, 0, 0]) linear_extrude(bearing_recess + 0.01)
    hull() for (z = [0, cheek_r + 2]) translate([0, z]) circle(d = boss_d + 1);
}

// --- Limb tube with a fork at fork_z and a knuckle or wrist plate at the low end ------
module limb(p, fork_z, low, split_z, cable_z = undef) {
  fb = fork_z - fork_bridge_z - fork_bridge_t;  // underside of the fork bridge
  top = p[len(p) - 1];
  hw = servo_body[1] / 2 + fit;
  // inner void tapers at 45° under the fork and above the knuckle (no supports)
  z_ft = fb - wall - (max(top[1], top[2]) - wall - cap_small);
  z_lt = low == "knuckle"
    ? knuckle_len + 1 + max(p[0][1] - wall - hw,
                            yc + p[0][2] - wall - (pocket_y0 + channel_h),
                            pocket_y0 - (yc - p[0][2] + wall))
    : p[0][0];
  // coupler socket
  ends = [seg(p, split_z - sock_len - neck), seg(p, split_z), seg(p, split_z + sock_len + neck)];
  ba = min([for (e = ends) e[1]]) - sock_wall;
  bb = min([for (e = ends) e[2]]) - sock_wall;

  difference() {
    union() {
      loft(p);
      hull() { ring_slice(top); rect_slice(fb, fork_half_x, fork_y0, fork_y1); }
      translate([0, 0, fork_z]) fork();
      if (low == "knuckle") {
        knuckle(knuckle_len);
        hull() { rect_slice(knuckle_len - 0.01, stem_half, y_back, y_flange); ring_slice(p[0]); }
      } else {
        hull() { ring_slice(p[0]); ring_slice([p[0][0] - wrist_t, p[0][1], p[0][2]]); }
      }
    }
    // void
    loft(clip(p, z_lt, split_z - sock_len - neck), wall);
    loft(clip(p, split_z + sock_len + neck, z_ft), wall);
    hull() { ring_slice(seg(p, z_ft), wall); rect_slice(fb - wall, cap_small, yc - cap_small, yc + cap_small); }
    translate([0, yc, split_z - sock_len]) linear_extrude(2 * sock_len) ellipse(ba, bb);
    translate([0, yc, split_z - sock_len - neck - 0.01]) linear_extrude(2 * (sock_len + neck) + 0.02)
      ellipse(ba - neck, bb - neck);
    translate([0, 0, fork_z]) fork_cuts();
    if (low == "knuckle") {
      knuckle_cuts(knuckle_len + 1);
      hull() {
        rect_slice(knuckle_len, hw, pocket_y0, pocket_y0 + channel_h);
        ring_slice(seg(p, z_lt), wall);
      }
    } else {
      z = p[0][0] - wrist_t - 1;
      translate([0, yc, z]) cylinder(d = wrist_hole_d, h = wrist_t + 2);
      for (x = [-1, 1], y = [-1, 1])
        translate([x * wrist_screws[0] / 2, yc + y * wrist_screws[1] / 2, z]) cylinder(d = 3.4, h = wrist_t + 2);
    }
    if (cable_z != undef)
      translate([0, yc, cable_z]) hole_x(cable_hole_d, -big / 2, 0, 1);
  }
}

module upper_arm() limb(upper_arm_profile, upper_arm_len, "knuckle", ua_split, cable_hole_z);
module forearm()   limb(forearm_profile, 0, "wrist", fa_split);

module shoulder_mount()
  difference() {
    union() {
      knuckle(mount_stem);
      hull() {
        rect_slice(mount_stem - 12, stem_half, y_back, y_flange);
        rect_slice(mount_stem - 0.01, stem_half + 10, y_back - 6, y_flange + 6);
      }
      translate([-plate[0] / 2, yc - plate[1] / 2, mount_stem]) cube(plate);
    }
    knuckle_cuts(mount_stem + plate[2] + 1);
    for (x = [-1, 1], y = [-1, 1])
      translate([x * plate_holes[0] / 2, yc + y * plate_holes[1] / 2, mount_stem - 1])
        cylinder(d = plate_hole_d, h = plate[2] + 2);
  }

// Coupler ring joining the two halves of a limb at split_z.
module coupler(p, split_z) {
  ends = [seg(p, split_z - sock_len - neck), seg(p, split_z), seg(p, split_z + sock_len + neck)];
  a = min([for (e = ends) e[1]]) - sock_wall - coupler_fit;
  b = min([for (e = ends) e[2]]) - sock_wall - coupler_fit;
  linear_extrude(2 * sock_len - 0.5) difference() {
    ellipse(a, b);
    ellipse(a - coupler_wall, b - coupler_wall);
  }
}

// Keep the part between z0 and z1.
module zcut(z0, z1) intersection() { children(); translate([-big / 2, -big / 2, z0]) cube([big, big, z1 - z0]); }

// Servo stand-in for the assembly preview.
module servo_ghost() {
  translate([-servo_body[1] / 2, y_flange - servo_below, -servo_shaft_off])
    cube([servo_body[1], servo_below + 12.5, servo_body[0]]);
  translate([-servo_body[1] / 2, y_flange, body_c - servo_flange_len / 2]) cube([servo_body[1], 2.5, servo_flange_len]);
  translate([0, y_horn, 0]) rotate([90, 0, 0]) cylinder(d = 24, h = 3);
}
