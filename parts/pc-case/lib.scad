// PC case: shared geometry in the world frame (params.scad). Part files include this and turn their piece into
// print orientation: lid and cover both print top down (flip_top / flip_lid).
include <params.scad>

eps = 0.01;
big = 1000;

// --- derived outline ---------------------------------------------------------------
cx1 = cooler_x[1] + side_gap + wall_t;     // cover's outer right
cy1 = cooler_y[1] + side_gap + wall_t;     // and back
lid_x1 = seam_x + wall_t + lid_ext;        // lid's edges past the seams (it reaches under the cover's bosses)
lid_y1 = seam_y + wall_t + lid_ext;
cb_full = z_lid1 + cover_boss[1];           // cover boss: full section up to here, then 45° into the wall
cb_nut  = z_lid1 + 4;                       // its nut's underside
// Circle through three points: the fan-side arc of the cooler outline.
function circ(p) = let(
  a = p[0], b = p[1], c = p[2],
  d = 2 * (a[0] * (b[1] - c[1]) + b[0] * (c[1] - a[1]) + c[0] * (a[1] - b[1])),
  s = [for (q = p) q[0] * q[0] + q[1] * q[1]],
  ux = (s[0] * (b[1] - c[1]) + s[1] * (c[1] - a[1]) + s[2] * (a[1] - b[1])) / d,
  uy = (s[0] * (c[0] - b[0]) + s[1] * (a[0] - c[0]) + s[2] * (b[0] - a[0])) / d)
  [[ux, uy], norm([ux, uy] - a)];
seam_c = circ(seam_pts);
assert(seam_x + wall_t / 2 + cover_boss[0] / 2 < cooler_x[0] - 1 && seam_y + wall_t / 2 + cover_boss[0] / 2 < cooler_y[0] - 1,
       "cover bosses hit the cooler");
assert(lid_x1 < cooler_x[0] && lid_y1 < cooler_y[0], "lid reaches under the cooler");
assert(cb_nut - nut_slot[0] / 2 > z_lid1, "nut pocket's 45° floor cuts into the lid");
assert(cb_full + (cover_boss[0] - wall_t) / 2 > z_ceil, "cover boss must reach the cover's top");

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
function seam_screws() = concat([for (y = seam_screws_x) [seam_x + wall_t / 2, y]], [for (x = seam_screws_y) [x, seam_y + wall_t / 2]]);

// --- lid: flat lattice plate between the tray walls, top flush with them ------------------
function lid_nodes() = [for (x = grid_x, y = grid_y)
  if (x - boss_d / 2 >= perim && y - boss_d / 2 >= perim
      && x + boss_d / 2 <= (y + boss_d / 2 < lid_y1 - perim ? board[0] : lid_x1) - perim
      && y + boss_d / 2 <= (x + boss_d / 2 < lid_x1 - perim ? board[1] : lid_y1) - perim) [x, y]];
module lid_bars_2d() {
  for (x = grid_x) rect([x - bar / 2, x + bar / 2], [shield_y - bar / 2, big]);
  for (x = shield_xs) rect([x - bar / 2, x + bar / 2], [-big, big]);
  for (y = grid_y) rect([-big, big], [y - bar / 2, y + bar / 2]);
}
module lid_plate_2d() difference() {
  lid_2d();
  difference() {
    offset(r = 3) offset(delta = -3) difference() { offset(delta = -perim) lid_2d(); lid_bars_2d(); }
    for (n = lid_nodes()) translate(n) circle(d = boss_d + 4);   // ring of plate round each mount: no shared face
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
module lid_mount(n) translate([n[0], n[1], z_lid - boss_h]) cylinder(d = boss_d, h = boss_h + 1);
module lid_mount_cut(n) translate([n[0], n[1], 0]) {
  translate([0, 0, z_lid - boss_h - 1]) cylinder(d = nut_press / cos(30), h = nut_t + 0.1 + 1, $fn = 6);
  translate([0, 0, z_lid - boss_h - 1]) cylinder(d = m3_clear, h = boss_h + skin_t + 2);
}
module lid() difference() {
  union() {
    translate([0, 0, z_lid]) linear_extrude(skin_t) lid_plate_2d();
    lid_slit_tabs();
    for (n = lid_nodes()) lid_mount(n);
  }
  for (n = lid_nodes()) lid_mount_cut(n);
  for (q = seam_screws()) translate([q[0], q[1], z_lid - 1]) cylinder(d = m3_clear, h = skin_t + 2);   // cover screws, from below
}

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
// Screw boss on a seam wall, local frame: wall centred on u = 0 along v, cover inside towards +u. The screw comes up
// through the lid; the nut slides in from inside the cover. Full section, then 45° into the wall (in print the
// cover is upside down: the taper is what the boss grows out of).
module cover_boss_local() let(w = cover_boss[0]) hull() {
  box([-w / 2, w / 2], [-w / 2, w / 2], [z_lid1 - 1, cb_full]);
  box([-wall_t / 2, wall_t / 2], [-w / 2, w / 2], [cb_full, cb_full + (w - wall_t) / 2]);
}
// Nut pocket, open towards +u; its floor (in use) is a 45° V: in print that's the roof, no bridge.
module cover_boss_cut_local() {
  translate([0, 0, z_lid1 - 1]) cylinder(d = m3_clear, h = cb_full - z_lid1);
  translate([-nut_slot[0] / cos(30) / 2, 0, 0]) rotate([90, 0, 90]) linear_extrude(big)
    polygon([[-nut_slot[0] / 2, cb_nut + nut_slot[1]], [-nut_slot[0] / 2, cb_nut], [0, cb_nut - nut_slot[0] / 2],
             [nut_slot[0] / 2, cb_nut], [nut_slot[0] / 2, cb_nut + nut_slot[1]]]);
}
module at_seam_screws() {
  for (y = seam_screws_x) translate([seam_x + wall_t / 2, y, 0]) children();
  for (x = seam_screws_y) translate([x, seam_y + wall_t / 2, 0]) rotate(90) children();
}
// One solid: outline extruded, inside hollowed up to the top; bosses added; then everything over the tray cut away
// below the lid's top and over the silver plate below its clearance. (Separate walls and top left coplanar faces.)
module cover() let(zv = [vent_margin, z_ceil - vent_margin]) difference() {
  union() {
    difference() {
      linear_extrude(z_top) cover_2d();
      translate([0, 0, -1]) linear_extrude(z_ceil + 1) rect([seam_x + wall_t, cx1 - wall_t], [seam_y + wall_t, cy1 - wall_t]);
      translate([0, 0, z_ceil - 1]) linear_extrude(skin_t + 2) cover_openings_2d();
    }
    at_seam_screws() cover_boss_local();
  }
  box([-big, tray_wall_x + tray_gap], [-big, board[1] + tray_gap], [-1, z_lid1]);
  box([-big, plate_x1 + tray_gap], [board[1] + tray_gap, big], [-1, plate_clear]);
  box([board[0], cable_cover_x], [seam_y - 1, seam_y + wall_t + 1], [-1, z_lid1 + 4]);   // right ribbons
  at_seam_screws() cover_boss_cut_local();
  // exhaust over the heatsink's outlet face, windows elsewhere
  vents_y([fins_y[0] + slot_w / 2 : slot_pitch : fins_y[1] - slot_w / 2], cx1 - wall_t - 1, cx1 + 1, zv[0], zv[1], slot_w);
  vents_x(vent_pos(plate_x1 + 14, cx1 - 20, [], 0), cy1 - wall_t, cy1, zv[0], zv[1]);   // past the plate: full height
  vents_x(vent_pos(cable_cover_x + 14, cx1 - 20, [], 0), seam_y, seam_y + wall_t, zv[0], zv[1]);
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
module cooler_ghost() linear_extrude(cooler_h) cooler_hw_2d();
