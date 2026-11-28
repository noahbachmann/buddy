// Skeletons of the PC case parts: the real parts (../lib.scad) cut down to what decides the fit: outlines, the
// lid's slit tabs, every screw boss, tab and hole, a few top-module nut pockets. Same geometry, a fraction of the
// filament: print these, assemble them on the hardware, then print the real parts.
include <../../src/lib.scad>

// --- skeleton params ---------------------------------------------------------------
skel_band  = 6;          // rim kept round the cover-top and cover-bottom outlines
skel_lid_band = perim + 0.5;   // lid: its solid rim and 0.5 into the openings (holds the seam screws; past the fillets' start)
skel_post  = 14;         // full-height wall strip kept at each cover corner
skel_joint = 17;         // wall strip / pad kept round each screw (18 put an edge on an exhaust slit's: pinched STL)
skel_top   = 3;          // cover-top: rim kept down from its top (skin + top of the walls)
skel_bars_x = [100];     // lid: cross bars kept for stiffness, with their top-module nut pockets (press-fit test)
skel_bars_y = [140];

// Rim of an outline, reaching past it: keep-regions must not share a face with the part (pinched STL).
module band_2d(w) difference() { offset(delta = 5) children(); offset(delta = -w) children(); }

// --- lid: rim (with the slit tabs and the seam screws), a cross with its mounts -------------
module lid_skeleton() intersection() {
  lid();
  translate([0, 0, -1]) linear_extrude(big) {
    band_2d(skel_lid_band) lid_2d();
    for (x = skel_bars_x) rect([x - boss_d / 2 - 1, x + boss_d / 2 + 1], [-big, big]);   // whole bosses along the bar; no shared faces
    for (y = skel_bars_y) rect([-big, big], [y - boss_d / 2 - 1, y + boss_d / 2 + 1]);
    for (q = seam_screws()) translate(q) circle(d = seam_boss_d + 3);   // seam nut bosses: whole, with plate round them
    rect([-1, slit_w + 1], slit_tabs_left);                       // slit tabs (inside the rim anyway)
    rect([board[0] - slit_w - 1, board[0] + 1], slit_tabs_right);
  }
}

// --- cover-top: top rim, corner posts, wall strips at every joint ---------------------------
// Right-wall strip round y, its ends in the middle of exhaust slits (an end next to a slit left a 0.5 mm sliver).
slit_c0 = fins_y[0] + slot_w / 2;
function slit_span(y) = [slit_c0 + slot_pitch * floor((y - skel_joint / 2 - slit_c0) / slot_pitch),
                         slit_c0 + slot_pitch * ceil((y + skel_joint / 2 - slit_c0) / slot_pitch)];
// Corners, plus where the walls step down: past the ribbon opening (a post on its edge left a 0.5 mm sliver) and
// at the silver plate.
function cover_corners() = [[pw_x0, cy1], [cx1, cy1], [cx1, seam_y], [cable_cover_x + 1 + skel_post / 2, seam_y],
                            [plate_x1 + tray_gap, cy1]];
module cover_top_skeleton() intersection() {
  cover();
  union() {
    translate([0, 0, z_ceil - skel_top]) linear_extrude(big) band_2d(skel_band + wall_t) cover_2d();
    translate([0, 0, -1]) linear_extrude(big) {
      for (c = cover_corners()) translate(c) square(skel_post, center = true);
      for (q = seam_screws()) translate(q) square(skel_joint, center = true);
      for (x = bottom_tabs_front) translate([x, seam_y]) square(skel_joint, center = true);
      for (x = bottom_tabs_back) translate([x, cy1]) square(skel_joint, center = true);
      for (y = bottom_tabs_right) rect([cx1 - skel_joint / 2, cx1 + skel_joint / 2], slit_span(y));
    }
  }
}

// --- cover-bottom: floor rim, the whole fan floor (sets the fan's height) and the nut tabs ---------
module cover_bottom_skeleton() intersection() {
  cover_bottom();
  translate([0, 0, -1]) linear_extrude(big) {
    band_2d(skel_band) cover_floor_2d();
    offset(delta = 1) fan_floor_2d();
    offset(delta = 2) bottom_tabs_2d();
  }
}
