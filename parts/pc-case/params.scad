// PC case for the HP all-in-one board, cooler and tray (#87). All dimensions in mm.
// World frame (use orientation): x left → right, y front (metal shield edge) → back (antenna edge), z up from the
// table; origin = the tray's front-left bottom corner. Part files build in this frame and turn into print orientation.
// Measured by Dev (2026-11-24) unless marked ⚠️ (scaled from the photo, ±4 mm: check with fit-check).

$fn = 64;

// --- hardware -------------------------------------------------------------------
board    = [250, 210];   // tray outline = the lid's outline
board_h  = 28;           // the tray's own walls: the lid lies on them
cooler_x = [188, 350];   // 162 wide; right edge 100 past the tray's right edge
cooler_y = [122, 250];   // 128 deep at the heatsink; back edge 40 past the tray's back edge
cooler_h = 38;           // fan and heatsink alike, standing on the table
// Fan side of the cooler's outline: an arc through these points (left edge, fan column at 115 deep, heat pipe corner).
// The housing's left side is 200 from the tray's left edge; the 12 mm left of it is its bracket strip.
seam_pts = [[188, 185], [235, 135], [258, 122]];   // ⚠️
fan_c    = [239, 196];   // ⚠️ fan hub
fan_in_r = 26;           // ⚠️ intake opening around the hub
fins_y   = [128, 246];   // ⚠️ heatsink outlet face (right side, x 350)

// cable notches in the lid's edges (green in the photo) ⚠️
cable_left  = [58, 126];   // y
cable_right = [85, 112];   // y
cable_back  = [160, 176];  // x: antenna wires
cable_d     = 6;           // notch depth into the lid

// --- stack-up -------------------------------------------------------------------
skin_t   = 2.4;          // lid plate, cover top
wall_t   = 2.4;          // walls, steps of 0.4 (docs/3d-printing.md)
z_lid    = board_h;                    // lid's underside, on the tray walls
z_lid1   = z_lid + skin_t;             // lid's top
head_gap = 0.6;          // over the cooler
z_ceil   = cooler_h + head_gap;        // cover's underside
z_top    = z_ceil + skin_t;            // cover's top: the highest point (41)

// --- outline --------------------------------------------------------------------
tray_gap = 0.5;          // tray to the cover's walls
side_gap = 1;            // cooler to the cover's walls (right, back)
// Lid ↔ cover split: two straight seams, far enough from the cooler for the seam wall and a screw head.
seam_x = cooler_x[0] - 9;   // vertical seam (for y ≥ seam_y)
seam_y = cooler_y[0] - 9;   // horizontal seam (for x ≥ seam_x)

// --- M3 hardware ---------------------------------------------------------------
m3_clear  = 3.4;         // screw passes: clearance, not a fit
nut_af    = 5.5;
nut_t     = 2.4;
nut_slot  = [nut_af + 0.2, nut_t + 0.3];   // slip +0.10/side (vertical, Fits), thickness like arm's nut slots
nut_press = nut_af + 0.1;                 // press fit +0.05/side: top-module nuts stay in from below
tab_t     = 5.6;         // tab with a nut slot: 2.7 slot + ~1.45 each side
tab_w     = 14;
tab_gap   = 0.2;         // lid tab to the cover's seam wall
head_room = 3.6;         // M3 head height + play, between the seam wall and the cooler

// --- lid ------------------------------------------------------------------------
perim  = 10;             // solid band round the lid plate (rests on the tray walls, carries the seam tabs)
bar    = 6;              // lattice bars
grid_x = [20, 60, 100, 140, 180, 220];   // lattice lines
grid_y = [60, 100, 140, 180];
shield_y   = 60;         // front band over the metal shield: open, only these bars cross it
shield_xs  = [100, 180];
boss_d     = 11;         // top-module mounts on the lattice nodes, standing on the lid
boss_h     = 4;          // above the lid; nut pocket from below
seam_tab_h = 8;          // lid tabs standing at the seams, under the cover's seam walls' screws
seam_tabs_x = [150, 196];   // y positions on the vertical seam
seam_tabs_y = [200, 232];   // x positions on the horizontal seam

// --- cover ----------------------------------------------------------------------
cover_grid_x = [220, 260, 300, 330];   // cover lattice lines
cover_grid_y = [150, 190, 220];

// --- airflow --------------------------------------------------------------------
slot_w     = 3;          // grille slits
slot_pitch = 6;
vent_w     = 10;         // wall windows (material + air)
vent_pitch = 14;
vent_margin = 8;         // solid wall above / below the windows
