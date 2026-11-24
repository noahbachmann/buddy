// PC case: shared geometry in the world frame (params.scad). Part files include this and turn their piece into
// print orientation: lid and cooler-top print top skin down (flip_top), base and cooler-bottom as they stand.
include <params.scad>

eps = 0.01;
big = 1000;

// --- derived outline ---------------------------------------------------------------
cx1 = cooler_x[1] + side_gap + wall_t;     // cooler shell's outer right
cy1 = cooler_y[1] + side_gap + wall_t;     // and back
// Circle through three points: the fan-side arc of the cooler outline.
function circ(p) = let(
  a = p[0], b = p[1], c = p[2],
  d = 2 * (a[0] * (b[1] - c[1]) + b[0] * (c[1] - a[1]) + c[0] * (a[1] - b[1])),
  s = [for (q = p) q[0] * q[0] + q[1] * q[1]],
  ux = (s[0] * (b[1] - c[1]) + s[1] * (c[1] - a[1]) + s[2] * (a[1] - b[1])) / d,
  uy = (s[0] * (c[0] - b[0]) + s[1] * (a[0] - c[0]) + s[2] * (b[0] - a[0])) / d)
  [[ux, uy], norm([ux, uy] - a)];
seam_c = circ(seam_pts);
assert(norm([cooler_x[0], cooler_y[0]] - seam_c[0]) > seam_c[1], "arc must cut the cooler's front-left corner");
assert(seam_x + plain_t + head_room <= cooler_x[0] && seam_y + plain_t + head_room <= cooler_y[0], "no room for screw heads");
assert(z_top - seam_tab_h >= z_board + 1, "seam tabs touch the board");

// --- helpers -----------------------------------------------------------------------
module box(xr, yr, zr) translate([xr[0], yr[0], zr[0]]) cube([xr[1] - xr[0], yr[1] - yr[0], zr[1] - zr[0]]);
module rect(xr, yr) translate([xr[0], yr[0]]) square([xr[1] - xr[0], yr[1] - yr[0]]);
// Use orientation → print orientation for parts that print top skin down (a rotation: no mirror image).
module flip_top() rotate([180, 0, 0]) translate([0, 0, -z_top]) children();
// Teardrop for a horizontal hole, tip towards +z (tip = 1) or −z (tip = −1) in use: the tip points up in print.
module teardrop2d(d, tip) rotate(tip < 0 ? 180 : 0) hull() { circle(d = d); rotate(45) square(d / 2); }
// Horizontal holes: along x through [x0, x1] at (y, z); along y through [y0, y1] at (x, z).
module hole_x(y, z, x0, x1, tip, d = m3_clear)
  translate([x0, y, z]) rotate([90, 0, 90]) linear_extrude(x1 - x0) teardrop2d(d, tip);
module hole_y(x, z, y0, y1, tip, d = m3_clear)
  translate([x, y1, z]) rotate([90, 0, 0]) linear_extrude(y1 - y0) teardrop2d(d, tip);
// Nut slot for a screw along an axis, open towards +z (open = 1) or −z (open = −1): the nut slides in from there.
// Hexagon with flats facing the slot walls; the closed end is a corner (a V floor in print, no bridge).
module nut_slot_2d(open, len)
  hull() { rotate(90) circle(d = nut_slot[0] / cos(30), $fn = 6); translate([-nut_slot[0] / 2, min(0, open * len)]) square([nut_slot[0], abs(len)]); }
module nut_slot_x(x, y, z, open, len)   // axis x, slot thickness along x centred on x
  translate([x - nut_slot[1] / 2, y, z]) rotate([90, 0, 90]) linear_extrude(nut_slot[1]) nut_slot_2d(open, len);
module nut_slot_y(x, y, z, open, len)
  translate([x, y + nut_slot[1] / 2, z]) rotate([90, 0, 0]) linear_extrude(nut_slot[1]) nut_slot_2d(open, len);
// Window in a wall: vertical slot with 45° points at both ends (no bridge in either print orientation).
module vent_2d(w, h) hull() { translate([0, w / 2]) square([eps, h - w], center = false); for (z = [w / 2, h - w / 2]) translate([0, z]) rotate(45) square(w / sqrt(2), center = true); }
module vents_x(xs, y0, y1, z0, z1)   // windows through a wall normal to y, centred on xs
  for (x = xs) translate([x, y1 + eps, z0]) rotate([90, 0, 0]) linear_extrude(y1 - y0 + 2 * eps) vent_2d(vent_w, z1 - z0);
module vents_y(ys, x0, x1, z0, z1, w = vent_w)
  for (y = ys) translate([x0 - eps, y, z0]) rotate([90, 0, 90]) linear_extrude(x1 - x0 + 2 * eps) vent_2d(w, z1 - z0);
// Window centres from a to b, skipping anything within `clear` of `avoid`.
function vent_pos(a, b, avoid, clear) =
  [for (p = [a : vent_pitch : b]) if (len(avoid) == 0 || min([for (q = avoid) abs(p - q)]) > clear) p];
// Same, also skipping any [lo, hi] range in `spans` (cable notches).
function vent_pos_spans(a, b, avoid, clear, spans) =
  [for (p = vent_pos(a, b, avoid, clear)) if (len([for (s = spans) if (p > s[0] - vent_w && p < s[1] + vent_w) 1]) == 0) p];
// Parallel slits (along y) filling a 2D region.
module slits_2d(xr, yr) for (x = [xr[0] : slot_pitch : xr[1] - slot_w]) rect([x, x + slot_w], yr);

// --- regions (2D) --------------------------------------------------------------------
module board_2d() rect([0, board[0]], [0, board[1]]);
module cooler_hw_2d() intersection() { rect(cooler_x, cooler_y); translate(seam_c[0]) circle(r = seam_c[1], $fn = 720); }
module cooler_box_2d() rect([seam_x, cx1], [seam_y, cy1]);               // cooler-top's whole top
module lid_2d() difference() { rect([0, board[0]], lid_y); rect([seam_x - seam_gap, big], [seam_y - seam_gap, big]); }
// 0.01 short of the front/back walls' inner faces: a cut ending in a face's plane pinches the STL
module tray_keepout() box([-1, board[0] + tray_gap], [lid_y[0] + wall_t + 0.01, lid_y[1] - wall_t - 0.01], [-1, z_base + rail_h + rail_gap]);
module floor_2d() difference() { cooler_box_2d(); offset(delta = tray_gap) board_2d(); }

// --- lid ---------------------------------------------------------------------------
lid_xr = [0, board[0]];
lid_back_x1 = seam_x - seam_gap;
lid_right_y1 = seam_y - seam_gap;
seam_tab_z = z_top - skin_t - (seam_tab_h - skin_t) / 2;   // screw height in the seam tabs
function lid_nodes() = [for (x = grid_x, y = grid_y)
  if (x - boss_d / 2 >= perim && x + boss_d / 2 <= (y < lid_right_y1 ? board[0] : lid_back_x1) - perim
      && y + boss_d / 2 <= (x < lid_back_x1 ? lid_y[1] : lid_right_y1) - perim) [x, y]];

module lid_bars_2d() {
  for (x = grid_x) rect([x - bar / 2, x + bar / 2], [shield_y - bar / 2, big]);
  for (x = shield_xs) rect([x - bar / 2, x + bar / 2], [-big, big]);
  for (y = grid_y) rect([-big, big], [y - bar / 2, y + bar / 2]);
}
// Skin openings: lattice cells, solid under the top-module mounts.
module lid_openings_2d() difference() {
  offset(r = 3) offset(delta = -3) difference() { offset(delta = -perim) lid_2d(); lid_bars_2d(); }
  for (n = lid_nodes()) translate(n) circle(d = boss_d + 4);   // ring of skin round the boss: no shared face
}
// Inside of the walls. No wall on the seam sides: the rect runs past lid_2d there.
module lid_interior_2d() rect([wall_t, board[0] - wall_t], [lid_y[0] + wall_t, lid_y[1] - wall_t]);
// Seam tabs: hang from the skin next to the cooler-top's, nut slot open downwards.
module lid_tabs_2d() {
  for (y = seam_tabs_x) rect([lid_back_x1 - tab_t, lid_back_x1], [y - tab_w / 2, y + tab_w / 2]);
  for (x = seam_tabs_y) rect([x - tab_w / 2, x + tab_w / 2], [lid_right_y1 - tab_t, lid_right_y1]);
}
// One solid for skin, walls and tabs: outline extruded, inside hollowed down from the skin, tabs left standing.
// Separate unions left coplanar faces at the wall tops (STL not watertight).
module shell(z_tab) difference() {
  translate([0, 0, z_base]) linear_extrude(z_top - z_base) children(0);
  translate([0, 0, -1]) linear_extrude(z_ceil + 1) difference() { children(1); children(2); }
  translate([0, 0, -1]) linear_extrude(z_tab + 1) intersection() { children(1); children(2); }
  translate([0, 0, z_ceil - 1]) linear_extrude(skin_t + 2) children(3);
  tray_keepout();
}
module lid_seam_cuts() {
  for (y = seam_tabs_x) {
    hole_x(y, seam_tab_z, lid_back_x1 - tab_t - 1, lid_back_x1 + 1, -1);
    nut_slot_x(lid_back_x1 - tab_t / 2, y, seam_tab_z, -1, big);
  }
  for (x = seam_tabs_y) {
    hole_y(x, seam_tab_z, lid_right_y1 - tab_t - 1, lid_right_y1 + 1, -1);
    nut_slot_y(x, lid_right_y1 - tab_t / 2, seam_tab_z, -1, big);
  }
}
module lid_vents() {
  zf = [z_base + vent_margin, z_top - skin_t - vent_margin];
  zs = [z_base + rail_h + vent_margin, zf[1]];   // side walls end at the tray rails
  vents_x(vent_pos(20, board[0] - 20, base_tab_xs_front, tab_w / 2 + vent_w), lid_y[0], lid_y[0] + wall_t, zf[0], zf[1]);
  vents_x(vent_pos(20, lid_back_x1 - 20, concat(base_tab_xs_back, [(cable_back[0] + cable_back[1]) / 2]), tab_w / 2 + vent_w),
          lid_y[1] - wall_t, lid_y[1], zf[0], zf[1]);
  vents_y(vent_pos_spans(20, board[1] - 20, [], 0, [cable_left]), 0, wall_t, zs[0], zs[1]);
  vents_y(vent_pos_spans(20, lid_right_y1 - 20, [], 0, [cable_right]), board[0] - wall_t, board[0], zs[0], zs[1]);
}
module lid_cable_cuts() {
  zr = [-1, z_base + rail_h + cable_h];
  box([-1, wall_t + 1], cable_left, zr);
  box([board[0] - wall_t - 1, board[0] + 1], cable_right, zr);
  box(cable_back, [lid_y[1] - wall_t - 1, lid_y[1] + 1], [-1, z_base + cable_h]);
}
module lid_mounts() for (n = lid_nodes()) translate([n[0], n[1], z_ceil - boss_h]) cylinder(d = boss_d, h = boss_h + eps);
module lid_mount_cuts() for (n = lid_nodes()) translate([n[0], n[1], 0]) {
  translate([0, 0, z_ceil - boss_h - 1]) cylinder(d = m3_clear, h = boss_h + skin_t + 2);
  translate([0, 0, z_ceil - boss_h - 1]) cylinder(d = nut_press / cos(30), h = nut_t + 0.1 + 1, $fn = 6);
}
module lid_screw_cuts() {   // lid ↔ base, through the front and back walls
  for (x = base_tab_xs_front) hole_y(x, screw_z_base, lid_y[0] - 1, lid_y[0] + wall_t + 1, -1);
  for (x = base_tab_xs_back) hole_y(x, screw_z_base, lid_y[1] - wall_t - 1, lid_y[1] + 1, -1);
}
module lid() difference() {
  union() {
    shell(z_top - seam_tab_h) { lid_2d(); lid_interior_2d(); lid_tabs_2d(); lid_openings_2d(); }
    lid_mounts();
  }
  lid_seam_cuts();
  lid_vents();
  lid_cable_cuts();
  lid_mount_cuts();
  lid_screw_cuts();
}

// --- base: frame under the tray, tabs the lid screws to ------------------------------
function base_tab_y0(front) = front ? -tray_gap - tab_t : board[1] + tray_gap;
module base_tab_2d(x, front) let(y0 = base_tab_y0(front))
  rect([x - tab_w / 2 - 1, x + tab_w / 2 + 1], front ? [lid_y[0], y0 + tab_t + 1] : [y0 - 1, lid_y[1]]);   // wider than the upper
module base_tab(x, front) let(y0 = base_tab_y0(front)) difference() {
  box([x - tab_w / 2, x + tab_w / 2], [y0, y0 + tab_t], [z_base / 2, base_tab_top]);   // starts inside the ribs
  hole_y(x, screw_z_base, y0 - 1, y0 + tab_t + 1, 1);
  nut_slot_y(x, y0 + tab_t / 2, screw_z_base, 1, big);
}
module base() {
  linear_extrude(z_base) {
    for (x = base_tab_xs_front) base_tab_2d(x, true);
    for (x = base_tab_xs_back) base_tab_2d(x, false);
    rect(lid_xr, [lid_y[0], lid_y[0] + wall_t + 0.2]);                    // under the lid's front wall
    rect([0, lid_back_x1], [lid_y[1] - wall_t - 0.2, lid_y[1]]);           // and back wall
    for (x = base_xs) rect([x - rib_w / 2, x + rib_w / 2], lid_y);
    rect([base_xr - rib_w / 2, base_xr + rib_w / 2], [lid_y[0], base_xr_y + rib_w / 2]);
    rect([base_xs[0], base_xr], [base_cross_y - rib_w / 2, base_cross_y + rib_w / 2]);
  }
  for (x = base_tab_xs_front) base_tab(x, true);
  for (x = base_tab_xs_back) base_tab(x, false);
}

// --- cooler-top ------------------------------------------------------------------------
// Lattice like the lid's; slits (no finger room) over the fan intake.
module cooler_top_openings_2d() {
  intersection() { translate(fan_c) circle(r = fan_in_r); slits_2d([fan_c[0] - fan_in_r, fan_c[0] + fan_in_r], [-big, big]); }
  offset(r = 3) offset(delta = -3) difference() {
    offset(delta = -perim) cooler_box_2d();
    translate(fan_c) circle(r = fan_in_r + bar);
    for (x = cooler_grid_x) rect([x - bar / 2, x + bar / 2], [-big, big]);
    for (y = cooler_grid_y) rect([-big, big], [y - bar / 2, y + bar / 2]);
  }
}
// Walls only where the cooler-top is outside: none along the seams, where the lid takes over.
module cooler_interior_2d() {
  rect([seam_x + wall_t, cx1 - wall_t], [seam_y + wall_t, cy1 - wall_t]);
  rect([seam_x - 1, seam_x + wall_t + 1], [seam_y - 1, lid_y[1] - wall_t]);       // left: lid's back wall ends here
  rect([seam_x - 1, board[0] - wall_t], [seam_y - 1, seam_y + wall_t + 1]);       // front: lid's right wall ends here
}
module cooler_tabs_2d() {
  for (y = seam_tabs_x) rect([seam_x, seam_x + plain_t], [y - tab_w / 2, y + tab_w / 2]);
  for (x = seam_tabs_y) rect([x - tab_w / 2, x + tab_w / 2], [seam_y, seam_y + plain_t]);
}
module cooler_top_cuts() {
  for (y = seam_tabs_x) hole_x(y, seam_tab_z, seam_x - 1, seam_x + plain_t + 1, -1);
  for (x = seam_tabs_y) hole_y(x, seam_tab_z, seam_y - 1, seam_y + plain_t + 1, -1);
  for (y = cb_tabs_left) hole_x(y, cb_screw_z, seam_x - 1, seam_x + wall_t + 1, -1);
  for (x = cb_tabs_bottom) hole_y(x, cb_screw_z, seam_y - 1, seam_y + wall_t + 1, -1);
  // exhaust: vertical slits over the heatsink's outlet face
  vents_y([fins_y[0] + slot_w / 2 : slot_pitch : fins_y[1] - slot_w / 2], cx1 - wall_t - 1, cx1 + 1,
          z_base + vent_margin, z_ceil - vent_margin, slot_w);
  vents_x(vent_pos(seam_x + 20, cx1 - 20, [], 0), cy1 - wall_t, cy1, z_base + vent_margin, z_ceil - vent_margin);
}
module cooler_top() difference() {
  shell(z_top - seam_tab_h) { cooler_box_2d(); cooler_interior_2d(); cooler_tabs_2d(); cooler_top_openings_2d(); }
  cooler_top_cuts();
}

// --- cooler-bottom ----------------------------------------------------------------------
cb_screw_z = z_base + cb_tab_h - 7;
module cb_tabs_2d() {
  for (y = cb_tabs_left) rect([seam_x, seam_x + wall_t + 0.2 + tab_t], [y - tab_w / 2, y + tab_w / 2]);
  for (x = cb_tabs_bottom) rect([x - tab_w / 2, x + tab_w / 2], [seam_y, seam_y + wall_t + 0.2 + tab_t]);
}
module cooler_bottom_floor_2d() difference() {
  floor_2d();
  offset(r = 3) offset(delta = -3) difference() {
    offset(delta = -perim) floor_2d();
    for (x = [270 : 40 : cx1]) rect([x - bar / 2, x + bar / 2], [-big, big]);
    for (y = [150 : 40 : cy1]) rect([-big, big], [y - bar / 2, y + bar / 2]);
    offset(delta = bar / 2) cb_tabs_2d();
  }
}
module cb_tab(p, along_x) difference() {   // along_x: screw along x (left wall), else along y (front wall)
  if (along_x) box([seam_x + wall_t + 0.2, seam_x + wall_t + 0.2 + tab_t], [p - tab_w / 2, p + tab_w / 2], [z_base / 2, z_base + cb_tab_h]);
  else box([p - tab_w / 2, p + tab_w / 2], [seam_y + wall_t + 0.2, seam_y + wall_t + 0.2 + tab_t], [z_base / 2, z_base + cb_tab_h]);
  if (along_x) {
    hole_x(p, cb_screw_z, seam_x, seam_x + wall_t + tab_t + 2, 1);
    nut_slot_x(seam_x + wall_t + 0.2 + tab_t / 2, p, cb_screw_z, 1, big);
  } else {
    hole_y(p, cb_screw_z, seam_y, seam_y + wall_t + tab_t + 2, 1);
    nut_slot_y(p, seam_y + wall_t + 0.2 + tab_t / 2, cb_screw_z, 1, big);
  }
}
module cooler_bottom() {
  linear_extrude(z_base) cooler_bottom_floor_2d();
  for (y = cb_tabs_left) cb_tab(y, true);
  for (x = cb_tabs_bottom) cb_tab(x, false);
}

// --- fit-check: flat template of the cooler corner, laid over the hardware ----------------
module fit_check() linear_extrude(1.6) {
  difference() { cooler_box_2d(); offset(delta = -2) cooler_box_2d(); }                 // cooler-top outline
  difference() { offset(delta = side_gap + 2) cooler_hw_2d(); offset(delta = side_gap) cooler_hw_2d(); }   // cooler + gap
  intersection() { cooler_box_2d(); union() {                                           // board edges
    rect([board[0] - 2, board[0]], [-big, big]);
    rect([-big, big], [board[1] - 2, board[1]]);
  } }
}

// --- ghosts for the assembly ------------------------------------------------------------
module board_ghost() box([0, board[0]], [0, board[1]], [z_base, z_board]);
module cooler_ghost() translate([0, 0, z_base]) linear_extrude(cooler_h) cooler_hw_2d();
