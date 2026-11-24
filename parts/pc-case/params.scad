// PC case for the HP all-in-one board, cooler and tray (#87). All dimensions in mm.
// World frame (use orientation): x left → right, y front (metal shield edge) → back (antenna edge), z up from the
// table; origin = the tray's front-left bottom corner. Part files build in this frame and turn into print orientation.
// Measured by Dev (2026-11-24) unless marked ⚠️ (scaled from the photo, ±4 mm: check with fit-check).

$fn = 64;

// --- hardware -------------------------------------------------------------------
board    = [250, 210];   // tray outline; 250 = the lid's outer width
board_h  = 28;           // tray + board + everything on it
cooler_x = [188, 350];   // 162 wide; right edge 100 past the board's right edge
cooler_y = [122, 250];   // 128 deep at the heatsink; back edge 40 past the board's back edge
cooler_h = 38;           // fan and heatsink alike
// Fan side of the cooler's outline: an arc through these points (left edge, fan column at 115 deep, heat pipe corner).
// The housing's left side is 200 from the board's left edge; the 12 mm left of it is its bracket strip.
seam_pts = [[188, 185], [235, 135], [258, 122]];   // ⚠️
fan_c    = [239, 196];   // ⚠️ fan hub
fan_in_r = 26;           // ⚠️ intake opening around the hub
fins_y   = [128, 246];   // ⚠️ heatsink outlet face (right side, x 350)

// cable openings (green in the photo) ⚠️
cable_left  = [58, 126];   // y, left wall
cable_right = [85, 112];   // y, right wall
cable_back  = [160, 176];  // x, back wall: antenna wires
cable_h     = 16;          // notch height from the wall's lower edge

// Tray side edges: the lid and cooler-top side walls sit over them (the lid hangs from the base tabs).
rail_h   = 5;            // ⚠️ not measured: height of the tray's side edge above its underside
rail_gap = 0.5;          // walls stop this far above it

// --- stack-up -------------------------------------------------------------------
floor_t  = 2.4;          // cooler floor and base ribs on the table (no feet: a raised floor would print as a bridge)
z_base   = floor_t;                   // tray and cooler bottoms
head_gap = 1;            // over the cooler (tallest)
skin_t   = 2.4;          // top skins
wall_t   = 2.4;          // walls, steps of 0.4 (docs/3d-printing.md)
z_ceil   = z_base + cooler_h + head_gap;
z_top    = z_ceil + skin_t;            // one flat top for lid and cooler
z_board  = z_base + board_h;           // highest point of the board

// --- outline --------------------------------------------------------------------
tray_gap = 0.5;          // tray to base tabs / bars
side_gap = 1;            // cooler to the cooler shell's walls (right, back)
seam_gap = 0.3;          // between lid and cooler-top skins
// Lid ↔ cooler-top split: two straight seams, far enough from the cooler for a tab pair and a screw head.
seam_x = cooler_x[0] - 9;   // vertical seam (for y ≥ seam_y)
seam_y = cooler_y[0] - 9;   // horizontal seam (for x ≥ seam_x)

// --- M3 hardware ---------------------------------------------------------------
m3_clear  = 3.4;         // screw passes: clearance, not a fit (tolerances stack over 250 mm)
nut_af    = 5.5;
nut_t     = 2.4;
nut_slot  = [nut_af + 0.2, nut_t + 0.3];   // slip +0.10/side (vertical, Fits), thickness like arm's nut slots
nut_press = nut_af + 0.1;                 // press fit +0.05/side: top-module nuts stay in when the lid is flipped
tab_t     = 5.6;         // tab with a nut slot: 2.7 slot + ~1.45 each side
tab_w     = 14;
plain_t   = 2.4;         // tab without a nut (the screw-head side)
head_room = 3.6;         // M3 head height + play, between a seam tab and the cooler

// --- lid ------------------------------------------------------------------------
lid_y = [-tray_gap - tab_t - 0.2 - wall_t, board[1] + tray_gap + tab_t + 0.2 + wall_t];   // front/back walls outside the base tabs
perim  = 8;              // solid band around the lid skin
bar    = 6;              // lattice bars
grid_x = [20, 60, 100, 140, 180, 220];   // lattice lines (x ≥ seam_x only where y < seam_y)
grid_y = [60, 100, 140, 180];
shield_y   = 60;         // front band over the metal shield: open, only these bars cross it
shield_xs  = [100, 180];
boss_d     = 11;         // top-module mount under each lattice node over the board
boss_h     = 4;          // below the skin; nut pocket from its lower face

// --- base -----------------------------------------------------------------------
rib_w     = 8;           // ribs under the tray (2.4 tall, lying flat)
base_xs   = [12, 125];   // full-depth ribs under the tray
base_xr   = 238;         // right rib, stops short of the fan (tray assumed notched there ⚠️)
base_xr_y = 104;
base_cross_y = 104;      // cross rib along x
base_tab_xs_front = [12, 125, 238];
base_tab_xs_back  = [12, 90, 165];
base_tab_top = z_base + 18;
screw_z_base = z_base + 11;

// --- cooler ---------------------------------------------------------------------
seam_tab_h = z_top - z_board - 1.4;   // seam tabs hang from the skin, ≥ 1.4 above the board
seam_tabs_x = [150, 196];            // y positions on the vertical seam
seam_tabs_y = [200, 232];            // x positions on the horizontal seam
cb_tabs_left   = [236];               // y: cooler-bottom tabs inside the cooler-top's left wall
cb_tabs_bottom = [282, 330];          // x: inside its bottom wall
cb_tab_h = 16;
cooler_grid_x = [220, 260, 300, 330];   // cooler-top lattice lines
cooler_grid_y = [150, 190, 220];

// --- airflow --------------------------------------------------------------------
slot_w     = 3;          // grille slits
slot_pitch = 6;
vent_w     = 10;         // wall windows (material + air)
vent_pitch = 14;
vent_margin = 8;         // solid wall above / below the windows
