// PC case for the HP all-in-one board, cooler and tray (#87). All dimensions in mm.
// World frame (use orientation): x left → right, y front (metal shield edge) → back (antenna edge), z up from the
// table; origin = the tray's front-left bottom corner. Part files build in this frame and turn into print orientation.
// Measured by Dev (2026-11-24) unless marked ⚠️ (scaled from the photo, ±4 mm: check with fit-check).

$fn = 64;

// --- hardware -------------------------------------------------------------------
board    = [250, 210];   // between the tray's side walls' inner faces (x) = the lid; 100 from the right one to the heatsink end
board_h  = 28;           // the tray's side walls: the lid's top is flush with them
tray_wall_x = 255;       // outer face of the tray's right wall (95 to the heatsink end)
// 3 mm slits between the board's edges and the tray's side walls: the lid's tabs drop in and stand on the silver
// plate the board sits on (board 8 mm above it)
slit_w    = 3;
slit_tab_t = 2.0;        // 0.5 play each side
slit_tabs_left  = [68, 103];   // ⚠️ y spans (slits seen 58–107; the left ribbon leaves at ~60)
slit_tabs_right = [62, 100];   // ⚠️
plate_z   = 1;           // ⚠️ top of the silver plate under the board: the tabs stand on it
// The silver plate continues behind the board (y > 210) to x ≈ 200, under the cover's left and back walls:
// they stop above it (and above the antenna wires running along the fan's edge).
plate_x1    = 200;       // ⚠️
plate_clear = 14;        // ⚠️ plate + its foam pads at the fan edge, with room for the antenna wires
cooler_x = [188, 350];   // 162 wide; right edge 100 past the tray's right edge
cooler_y = [122, 250];   // 128 deep at the heatsink; back edge 40 past the tray's back edge
cooler_h = 38;           // fan and heatsink alike, standing on the table
// Fan housing's front-left corner: an arc through these points, cut out of the cooler's rectangle. Read off the
// fit-check photo (2026-11-24); the bracket strip left of the housing reaches x 188.
seam_pts = [[195, 178], [203, 149], [226, 139]];   // ⚠️
fan_c    = [239, 196];   // ⚠️ fan hub
fan_in_r = 26;           // ⚠️ intake opening around the hub
fins_y   = [128, 246];   // ⚠️ heatsink outlet face (right side, x 350)

// cable notches in the lid's edges (green in the photo) ⚠️
cable_left  = [54, 66];    // y: ribbon out through the left slit
cable_right = [104, 128];  // y: ribbons out over the right tray wall
cable_back  = [160, 176];  // x: antenna wires
cable_d     = 6;           // notch depth into the lid

// --- stack-up -------------------------------------------------------------------
skin_t   = 2.4;          // lid plate, cover top
wall_t   = 2.4;          // walls, steps of 0.4 (docs/3d-printing.md)
z_lid1   = board_h;                    // lid's top, flush with the tray walls
z_lid    = z_lid1 - skin_t;            // lid's underside
head_gap = 0.6;          // over the cooler
z_ceil   = cooler_h + head_gap;        // cover's underside
z_top    = z_ceil + skin_t;            // cover's top: the highest point (41)

// --- outline --------------------------------------------------------------------
tray_gap = 0.5;          // tray to the cover's walls
lid_gap  = 0.3;          // lid edge to the tray walls' inner faces
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
head_room = 3.6;         // M3 head height + play

// --- lid ------------------------------------------------------------------------
perim  = 10;             // solid band round the lid plate (rests on the tray walls, carries the seam tabs)
bar    = 6;              // lattice bars
grid_x = [20, 60, 100, 140, 180, 220];   // lattice lines
grid_y = [60, 100, 140, 180];
shield_y   = 60;         // front band over the metal shield: open, only these bars cross it
shield_xs  = [100, 180];
boss_d     = 11;         // top-module mounts on the lattice nodes: bosses under the lid, nut pocket from below
boss_h     = 2;          // below the lid plate
lid_ext    = 4;          // lid reaches this far past the cover's seam walls, under its screw bosses
seam_screws_x = [150, 196];   // y positions on the vertical seam
seam_screws_y = [200, 232];   // x positions on the horizontal seam
cover_boss = [9, 8];     // screw boss on the cover's seam wall: width, full-section height above the lid
cable_cover_x = 262;     // cover's front wall open from the tray wall to here, up to z_lid1 + 4: right ribbons

// --- cover ----------------------------------------------------------------------
cover_grid_x = [220, 260, 300, 330];   // cover lattice lines
cover_grid_y = [150, 190, 220];

// --- airflow --------------------------------------------------------------------
slot_w     = 3;          // grille slits
slot_pitch = 6;
vent_w     = 10;         // wall windows (material + air)
vent_pitch = 14;
vent_margin = 8;         // solid wall above / below the windows
