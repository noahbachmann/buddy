// PC case: shared geometry in the world frame (params.scad). Part files include this and turn their piece into
// print orientation: lid and cover both print top down (flip_top / flip_lid).
include <params.scad>

eps = 0.01;
big = 1000;

// --- derived outline ---------------------------------------------------------------
cx1 = cooler_x[1] + right_gap + wall_t;    // cover's outer right
cy1 = cooler_y[1] + side_gap + wall_t;     // and back
lid_x1 = seam_x + wall_t + lid_ext;        // lid's edges past the seams (it reaches under the cover's bosses)
lid_y1 = seam_y + wall_t + lid_ext;
cb_head = z_top - head_room;                // seam screw head recess: floor, then a 45° cone to the clearance hole
// Circle through three points: the fan-side arc of the cooler outline.
function circ(p) = let(
  a = p[0], b = p[1], c = p[2],
  d = 2 * (a[0] * (b[1] - c[1]) + b[0] * (c[1] - a[1]) + c[0] * (a[1] - b[1])),
  s = [for (q = p) q[0] * q[0] + q[1] * q[1]],
  ux = (s[0] * (b[1] - c[1]) + s[1] * (c[1] - a[1]) + s[2] * (a[1] - b[1])) / d,
  uy = (s[0] * (c[0] - b[0]) + s[1] * (a[0] - c[0]) + s[2] * (b[0] - a[0])) / d)
  [[ux, uy], norm([ux, uy] - a)];
seam_c = circ(seam_pts);
seam_u = wall_t / 2 + seam_off;            // screw axis from the seam's outer face, into the cover
assert(seam_x + seam_u + cover_boss / 2 < cooler_x[0] - 1 && seam_y + seam_u + cover_boss / 2 < cooler_y[0] - 1,
       "cover bosses hit the cooler");
assert(lid_x1 < cooler_x[0] && lid_y1 < cooler_y[0], "lid reaches under the cooler");
assert(seam_x + seam_u + seam_boss_d / 2 < lid_x1 - 1 && seam_y + seam_u + seam_boss_d / 2 < lid_y1 - 1,
       "lid's seam nut boss reaches its edge");
assert(cb_head - (head_bore - m3_clear) / 2 > z_lid1 + 1, "head recess too deep: no cover under the head");
// screw from the head's seat (Ø5.5 on the cone) down: through the nut under the lid
assert(cb_head - (head_bore - 5.5) / 2 - seam_screw_l < z_lid - boss_h + 0.1, "seam screw doesn't pass the nut");
assert(cpu_zone_x[0] > perim && cpu_zone_y[1] < board[1] - wall_t - 1 && cpu_zone_x[1] < lid_x1 - perim,
       "CPU zone reaches the lid's edge band or back wall");
assert(z_lid1 - back_wall_h >= plate_z + 8, "back wall reaches below the board's top face");
bt_z = floor_t + 4.5;                       // cover-bottom ↔ cover-top screws
assert(floor_t + bottom_tab_h < hs_z0 - 1, "bottom tabs reach the heatsink");
assert(bt_z - nut_slot[0] / cos(30) / 2 > floor_t, "bottom tab nut slot cuts into the floor");

// --- helpers -----------------------------------------------------------------------
module box(xr, yr, zr) translate([xr[0], yr[0], zr[0]]) cube([xr[1] - xr[0], yr[1] - yr[0], zr[1] - zr[0]]);
module rect(xr, yr) translate([xr[0], yr[0]]) square([xr[1] - xr[0], yr[1] - yr[0]]);
// Use orientation → print orientation, top down (a rotation, no mirror image).
module flip_top() rotate([180, 0, 0]) translate([0, 0, -z_top]) children();
module flip_lid() rotate([180, 0, 0]) translate([0, 0, -z_lid1]) children();
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
// Cooler: its rectangle minus the fan housing's rounded front-left corner.
module cooler_hw_2d() difference() {
  rect(cooler_x, cooler_y);
  difference() { rect([-big, seam_c[0][0]], [-big, seam_c[0][1]]); translate(seam_c[0]) circle(r = seam_c[1], $fn = 180); }
}
module cover_2d() rect([seam_x, cx1], [seam_y, cy1]);
// Lid: between the tray walls, reaching under the cover's seam walls and bosses.
module lid_2d() difference() { rect([lid_gap, board[0] - lid_gap], [0, board[1]]); rect([lid_x1, big], [lid_y1, big]); }
// Screw positions lid ↔ cover, on the seam walls' centre lines.
function seam_screws() = concat([for (y = seam_screws_x) [seam_x + seam_u, y]], [for (x = seam_screws_y) [x, seam_y + seam_u]]);
module seam_bosses_2d() for (q = seam_screws()) translate(q) square(cover_boss, center = true);

// --- lid: flat lattice plate between the tray walls, top 6 above them ---------------------
function lid_nodes() = [for (x = grid_x, y = grid_y)
  if (x - boss_d / 2 >= perim && y - boss_d / 2 >= perim
      && x + boss_d / 2 <= (y + boss_d / 2 < lid_y1 - perim ? board[0] : lid_x1) - perim
      && y + boss_d / 2 <= (x + boss_d / 2 < lid_x1 - perim ? board[1] : lid_y1) - perim
      && !in_cpu_zone([x, y])) [x, y]];
function in_cpu_zone(q) = q[0] > cpu_zone_x[0] && q[0] < cpu_zone_x[1] && q[1] > cpu_zone_y[0] && q[1] < cpu_zone_y[1];
module lid_bars_2d() {
  for (x = concat(grid_x, fine_x)) rect([x - bar / 2, x + bar / 2], [shield_y - bar / 2, big]);
  for (x = shield_xs) rect([x - bar / 2, x + bar / 2], [-big, big]);
  for (y = concat(grid_y, fine_y)) rect([-big, big], [y - bar / 2, y + bar / 2]);
}
module lid_plate_2d() difference() {
  lid_2d();
  difference() {
    offset(r = 3) offset(delta = -3) difference() { offset(delta = -perim) lid_2d(); lid_bars_2d(); }
    for (n = lid_nodes()) translate(n) circle(d = boss_d + 4);   // ring of plate round each mount: no shared face
    for (q = seam_screws()) translate(q) circle(d = seam_boss_d + 4);
  }
  // cable notches: the cables come up past the lid's edges here
  rect([-1, cable_d], cable_left);
  rect([board[0] - cable_d, board[0] + 1], cable_right);
  rect(cable_back, [board[1] - cable_d, board[1] + 1]);
}
// Tabs into the slits beside the board, standing on the silver plate: they carry the lid.
module lid_slit_tabs() let(g = (slit_w - slit_tab_t) / 2) {
  box([g, g + slit_tab_t], slit_tabs_left, [plate_z, z_lid + 1]);
  box([board[0] - g - slit_tab_t, board[0] - g], slit_tabs_right, [plate_z, z_lid + 1]);
}
// Top-module mounts: bosses under the lattice nodes, nut pressed in from below (open upwards in print: no bridge).
module lid_mount(n, d = boss_d) translate([n[0], n[1], z_lid - boss_h]) cylinder(d = d, h = boss_h + 1);
module lid_mount_cut(n) translate([n[0], n[1], 0]) {
  translate([0, 0, z_lid - boss_h - 1]) cylinder(d = nut_press / cos(30), h = nut_t + 0.1 + 1, $fn = 6);
  translate([0, 0, z_lid - boss_h - 1]) cylinder(d = m3_clear, h = boss_h + skin_t + 2);
}
module lid() intersection() {   // one outline cut makes the plate's, back wall's and hooks' outer faces (no coplanar union)
 translate([0, 0, -big]) linear_extrude(big + z_lid1) { lid_2d(); lid_hooks_2d(); }   // also the one top face at z_lid1
 difference() {
  union() {
    translate([0, 0, z_lid]) linear_extrude(skin_t + 1) lid_plate_2d();   // top made by the outline cut
    lid_slit_tabs();
    lid_hooks();
    lid_back_wall();
    for (n = lid_nodes()) lid_mount(n);
    for (q = seam_screws()) lid_mount(q, seam_boss_d);   // cover screws come down into these
  }
  for (n = concat(lid_nodes(), seam_screws())) lid_mount_cut(n);
  box(cable_back, [board[1] - cable_d, board[1] + 1], [-1, z_lid + 1]);   // antenna notch through the back wall
  box(cpu_zone_x, cpu_zone_y, [z_lid - 1, z_lid1 - cpu_skin_t]);            // thinner over the CPU, from below
 }
}
// Front-edge hooks: an arm down past the tray's front edge, a hook under its silver plate. Arm and hook are one
// extruded profile (y, z); the arm runs into the plate's front band. Hook's underside slopes up to its tip: a lead-in
// that pushes the arm out when the lid is pressed down. In print (top down) its catching face is a 3 mm overhang.
hook_z0 = z_lid1 - hook_reach;
module lid_hooks_2d() for (x = hook_xs) rect([x - hook_w / 2, x + hook_w / 2], [-hook_gap - hook_arm_t, 1]);
module lid_hooks() for (x = hook_xs) translate([x - hook_w / 2, 0, 0]) rotate([90, 0, 90]) linear_extrude(hook_w)
  polygon([[-hook_gap - hook_arm_t, hook_z0], [-hook_gap, hook_z0], [-hook_gap + hook_len, hook_z0 + hook_t / 2],
           [-hook_gap + hook_len, hook_z0 + hook_t], [-hook_gap, hook_z0 + hook_t], [-hook_gap, z_lid + 1],
           [perim / 2, z_lid + 1], [perim / 2, z_lid1 + 1], [-hook_gap - hook_arm_t, z_lid1 + 1]]);
// Back edge, left side up to the cover's seam wall: hangs down from the plate (grows up in print).
module lid_back_wall() box([-1, lid_x1 + 1], [board[1] - wall_t, board[1] + 1], [z_lid1 - back_wall_h, z_lid + 1]);

// --- cover: over the cooler, standing on the table; seam walls stand on the lid --------------
module cover_openings_2d() {
  intersection() { translate(fan_c) circle(r = fan_in_r); slits_2d([fan_c[0] - fan_in_r, fan_c[0] + fan_in_r], [-big, big]); }
  offset(r = 3) offset(delta = -3) difference() {
    offset(delta = -perim) cover_2d();
    translate(fan_c) circle(r = fan_in_r + bar);
    for (x = cover_grid_x) rect([x - bar / 2, x + bar / 2], [-big, big]);
    for (y = cover_grid_y) rect([-big, big], [y - bar / 2, y + bar / 2]);
  }
}
// Seam screw from the cover's top: head recessed (45° cone under the recess: a roof in print, no bridge), clearance
// hole down to the lid. The bosses are square columns from the lid to the top, part of the cover's outline extrusion.
module cover_boss_cut() {
  translate([0, 0, z_lid1 - 1]) cylinder(d = m3_clear, h = z_top - z_lid1 + 2);
  translate([0, 0, cb_head]) cylinder(d = head_bore, h = head_room + 1);
  translate([0, 0, cb_head - (head_bore - m3_clear) / 2]) cylinder(d1 = m3_clear, d2 = head_bore + 2 * eps, h = (head_bore - m3_clear) / 2 + eps);
}
module at_seam_screws() for (q = seam_screws()) translate([q[0], q[1], 0]) children();
// One solid: outline (with the seam bosses) extruded, inside hollowed up to the top round the bosses; then everything
// over the tray cut away below the lid's top and over the silver plate below its clearance. (Separate walls, top and
// bosses left coplanar faces.)
module cover() let(zv = [vent_margin, z_ceil - vent_margin]) difference() {
  linear_extrude(z_top) { cover_2d(); seam_bosses_2d(); }
  translate([0, 0, -1]) linear_extrude(z_ceil + 1)
    difference() { rect([seam_x + wall_t, cx1 - wall_t], [seam_y + wall_t, cy1 - wall_t]); seam_bosses_2d(); }
  translate([0, 0, z_ceil - 1]) linear_extrude(cover_skin + 2) cover_openings_2d();
  box([-big, tray_wall_x + tray_gap], [-big, board[1] + tray_gap], [-1, z_lid1]);
  box([-big, plate_x1 + tray_gap], [board[1] + tray_gap, big], [-1, plate_clear]);
  box([board[0], cable_cover_x], [seam_y - 1, seam_y + wall_t + 1], [-1, z_lid1 + 4]);   // right ribbons
  box([-big, big], [-big, big], [-1, floor_t]);                                           // stands on the floor
  at_seam_screws() cover_boss_cut();
  // cover-bottom screws, from outside
  for (x = bottom_tabs_front) hole_y(x, bt_z, seam_y - 1, seam_y + wall_t + 1, -1);
  for (x = bottom_tabs_back) hole_y(x, bt_z, cy1 - wall_t - 1, cy1 + 1, -1);
  for (y = bottom_tabs_right) hole_x(y, bt_z, cx1 - wall_t - 1, cx1 + 1, -1);
  // exhaust over the heatsink's outlet face (it starts at hs_z0), windows elsewhere
  vents_y([fins_y[0] + slot_w / 2 : slot_pitch : fins_y[1] - slot_w / 2], cx1 - wall_t - 1, cx1 + 1, hs_z0 + 1, z_ceil - 4, slot_w);
  vents_x(vent_pos(plate_x1 + 14, cx1 - 20, bottom_tabs_back, tab_w / 2 + vent_w), cy1 - wall_t, cy1, zv[0], zv[1]);
  vents_x(vent_pos(cable_cover_x + 14, cx1 - 20, bottom_tabs_front, tab_w / 2 + vent_w), seam_y, seam_y + wall_t, zv[0], zv[1]);
}

// --- cover-bottom: lattice floor under the heatsink (raised) and in front of it, a thin solid one under the fan -----
module cover_floor_2d() difference() {
  cover_2d();
  rect([-big, tray_wall_x + tray_gap], [-big, board[1] + tray_gap]);          // tray
  rect([-big, plate_x1 + tray_gap], [board[1] + tray_gap, big]);              // silver plate
  fan_rect_2d();
}
module fan_rect_2d() rect([-big, fan_x1 + side_gap], [fan_y0 - side_gap, cooler_y[1] + tray_gap]);
// Under the fan, outside the tray and the silver plate (inside them the fan is over the board).
module fan_floor_2d() intersection() {
  fan_rect_2d();
  difference() {
    cover_2d();
    rect([-big, tray_wall_x + tray_gap], [-big, board[1] + tray_gap]);
    rect([-big, plate_x1 + tray_gap], [board[1] + tray_gap, big]);
  }
}
module bottom_tabs_2d() {
  for (x = bottom_tabs_front) rect([x - tab_w / 2, x + tab_w / 2], [seam_y, seam_y + wall_t + 0.2 + tab_t]);
  for (x = bottom_tabs_back) rect([x - tab_w / 2, x + tab_w / 2], [cy1 - wall_t - 0.2 - tab_t, cy1]);
  for (y = bottom_tabs_right) rect([cx1 - wall_t - 0.2 - tab_t, cx1], [y - tab_w / 2, y + tab_w / 2]);
}
module cover_bottom_2d() difference() {
  cover_floor_2d();
  difference() {
    offset(r = 3) offset(delta = -3) difference() {
      offset(delta = -perim) cover_floor_2d();
      for (x = cover_grid_x) rect([x - bar / 2, x + bar / 2], [-big, big]);
      for (y = cover_grid_y) rect([-big, big], [y - bar / 2, y + bar / 2]);
    }
    offset(delta = bar / 2) bottom_tabs_2d();
  }
}
module bottom_tab(along_x, p, t0) let(zr = [floor_t / 2, floor_t + bottom_tab_h], c = t0 + tab_t / 2) difference() {
  if (along_x) box([t0, t0 + tab_t], [p - tab_w / 2, p + tab_w / 2], zr);
  else box([p - tab_w / 2, p + tab_w / 2], [t0, t0 + tab_t], zr);
  if (along_x) { hole_x(p, bt_z, t0 - 1, t0 + tab_t + 1, 1); nut_slot_x(c, p, bt_z, 1, big); }
  else { hole_y(p, bt_z, t0 - 1, t0 + tab_t + 1, 1); nut_slot_y(p, c, bt_z, 1, big); }
}
// One extrusion stepped down under the fan: two touching floors left coplanar faces.
module cover_bottom() {
  difference() {
    linear_extrude(floor_t) { cover_bottom_2d(); fan_floor_2d(); }
    translate([0, 0, fan_floor_t]) linear_extrude(floor_t) fan_rect_2d();
  }
  for (x = bottom_tabs_front) bottom_tab(false, x, seam_y + wall_t + 0.2);
  for (x = bottom_tabs_back) bottom_tab(false, x, cy1 - wall_t - 0.2 - tab_t);
  for (y = bottom_tabs_right) bottom_tab(true, y, cx1 - wall_t - 0.2 - tab_t);
}

// --- fit-check: flat template of the cooler corner, laid over the hardware ----------------
module fit_check() linear_extrude(1.6) {
  difference() { cover_2d(); offset(delta = -2) cover_2d(); }                          // cover outline
  difference() { offset(delta = side_gap + 2) cooler_hw_2d(); offset(delta = side_gap) cooler_hw_2d(); }   // cooler + gap
  intersection() { cover_2d(); union() {                                                // tray edges
    rect([board[0] - 2, board[0]], [-big, big]);
    rect([-big, big], [board[1] - 2, board[1]]);
  } }
}

// --- ghosts for the assembly ------------------------------------------------------------
// Tray side walls (right wall ends where the fan begins ⚠️), silver plate under the board and behind it, cooler.
module tray_ghost() {
  box([board[0] - tray_wall_x, 0], [0, board[1]], [0, board_h]);
  box([board[0], tray_wall_x], [0, cooler_y[0]], [0, board_h]);
  box([board[0] - tray_wall_x, plate_x1], [0, 290], [0, plate_z]);
}
module cooler_ghost() translate([0, 0, cooler_z0]) {   // fan on the thin floor, heatsink raised
  intersection() { linear_extrude(cooler_h) cooler_hw_2d(); box([-big, fan_x1], [fan_y0, big], [0, cooler_h]); }
  intersection() { linear_extrude(cooler_h) cooler_hw_2d(); box([-big, fan_x1], [-big, fan_y0], [hs_dz, cooler_h]); }   // heat pipe, raised
  intersection() { linear_extrude(cooler_h) cooler_hw_2d(); box([fan_x1, big], [-big, big], [hs_dz, cooler_h]); }
}
