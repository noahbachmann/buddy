// PC case for the HP all-in-one board, cooler and tray (#87). All dimensions in mm.
// World frame (use orientation): x left → right, y front (metal shield edge) → back (antenna edge), z up from the
// table; origin = the tray's front-left bottom corner. Part files build in this frame and turn into print orientation.
// Measured by Dev (2026-11-24) unless marked ⚠️ (scaled from the photo, ±4 mm: check with fit-check).

$fn = 64;

// --- hardware -------------------------------------------------------------------
// Between the tray's side walls' inner faces (x) = the lid. Measured 250; the printed lid skeleton was one tab width
// short (Dev 2026-11-25): 252, then 0.6 more (Dev 2026-11-25, second skeleton). Grown on the right: the cooler stays
// put (95 from the wall's outer face, fit-check).
board    = [252.6, 210];
board_h  = 28;           // the tray's side walls
tray_wall_x = 255;       // outer face of the tray's right wall (95 to the heatsink end)
// 3 mm slits between the board's edges and the tray's side walls: the lid's tabs drop in and stand on the silver
// plate the board sits on (board 8 mm above it)
slit_w    = 3;
slit_tab_t = 2.0;        // 0.5 play each side
slit_tabs_left  = [68, 103];   // ⚠️ y spans (slits seen 58–107; the left ribbon leaves at ~60)
slit_tabs_right = [55.5, 89];  // ⚠️ was 62–100: 8 towards the front, 1.5 shorter (Dev 2026-11-26); back end 3 off the cooler (Dev 2026-11-27)
plate_z   = 1;           // ⚠️ top of the silver plate under the board: the tabs stand on it
// The silver plate continues behind the board (y > 210) to x ≈ 200, under the cover's left and back walls:
// they stop above it (and above the antenna wires running along the fan's edge).
plate_x1    = 200;       // ⚠️
plate_clear = 14;        // ⚠️ plate + its foam pads at the fan edge, with room for the antenna wires
cooler_x = [188, 350];   // 162 wide; right edge 95 past the tray's right wall (outer face)
cooler_y = [122, 250];   // 128 deep at the heatsink; back edge 40 past the tray's back edge
cooler_h = 38;           // fan and heatsink alike
fan_floor_t = 0.8;      // cover-bottom's floor under the fan: lies flat on the table, carries no bending (Dev 2026-11-25)
cooler_z0 = fan_floor_t; // the fan housing stands on it
hs_dz    = 13;           // ⚠️ heatsink's underside above the fan's base (ruler photo 2026-11-24): the floor runs under it
hs_z0    = cooler_z0 + hs_dz;
fan_x1   = 297;          // ⚠️ fan housing ↔ heatsink (fit-check photo): the fan stands on the table left of this
fan_y0   = 137;          // ⚠️ fan housing's front edge: the thin floor behind it
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
lid_top  = 34;           // lid's top: nearly the cover's height (Dev 2026-11-25); plate 3.6 above the tray walls
z_lid1   = lid_top;
z_lid    = z_lid1 - skin_t;            // lid's underside
head_gap   = 0.4;        // over the cooler
cover_skin = 1.2;        // cover's top: thinner than the lid, the case stays at ~40 (Dev: ≤ 40, 40.4 with the fan floor)
floor_t    = 1.6;        // cover-bottom's floor; the cover-top's walls stand on it
z_ceil   = cooler_z0 + cooler_h + head_gap;   // cover's underside
z_top    = z_ceil + cover_skin;        // cover's top: the highest point (40.4)

// --- outline --------------------------------------------------------------------
tray_gap = 0.5;          // tray to the cover's walls
lid_gap  = 0.3;          // lid edge to the tray walls' inner faces
side_gap = 1;            // cooler to the cover's back wall
// Right wall: the heat pipe's end nearly touched it (Dev 2026-11-26): room for felt/cork on the wall's inside
felt_t     = 3;          // self-adhesive felt sheet (or 2–3 mm cork)
right_gap  = side_gap + felt_t;   // cooler to the cover's right wall
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
seam_screw_l = 8;        // M3 × 8, lid ↔ cover

// --- lid ------------------------------------------------------------------------
perim  = 10;             // solid band round the lid plate (rests on the tray walls, carries the seam tabs)
bar    = 6;              // lattice bars
grid_x = [20, 60, 100, 140, 180, 220];   // lattice lines
grid_y = [60, 100, 140, 180];
fine_x = [40, 80, 120, 160, 200];   // bars between them, no mounts: ~14 mm windows over the board (Dev 2026-11-25)
fine_y = [80, 120, 160];
// Over the CPU block (Dev's photo 2026-11-26): more air between lid and heat. The top stays flat (prints top down:
// a raised patch would leave the rest of the plate floating), so the plate thins from below and the mounts go.
cpu_zone_x  = [38, 165];
cpu_zone_y  = [106, 197];  // starts past the y 100 bosses (no half-cut boss)
cpu_skin_t  = 1.2;         // plate there: underside 1.2 higher, nodes 3.2 (no boss)
back_wall_h = 25;        // wall down from the lid's top along its back edge, left to the cover: z 9 (Dev 2026-11-26: 3 deeper) ⚠️ board below
// Hooks down the front edge, under the tray's silver plate: the lid holds itself on the tray (Dev 2026-11-26).
// Dev measured: lid top 32 above the plate's underside at the front.
hook_xs    = [50, 215];  // centres; clear of the ribbons leaving over the front edge (~x 110–188, photo) ⚠️
hook_w     = 20;         // along the edge
hook_reach = 33.6;       // lid top → hook's bottom: 32 to the plate's underside + the hook
hook_t     = 1.6;        // hook height (its top catches the plate; Dev 2026-11-26)
hook_len   = 4;          // under the plate, from the arm's inner face (Dev 2026-11-27: 3 → 4)
hook_arm_t = 2;          // straight down from the lid's front edge (Dev 2026-11-26); flexes ~3 mm out to snap on (~0.8 % strain)
back_hook_xs  = [15, 80, 145];   // centres along the back wall, left of the antenna notch (x 160–176) ⚠️ board parts under them
back_hook_len = 4;       // from the back wall's inner face towards the front, over the board's back edge (Dev 2026-11-27)
back_hook_t   = 2;       // at the wall's bottom, inside its height: z 9–11
hook_gap   = lid_gap;    // play: arm's inner face to the tray plate's edge (y 0); the lid's front edge is out there too
shield_y   = 60;         // front band over the metal shield: open, only these bars cross it
shield_xs  = [100, 180];
boss_d     = 11;         // top-module mounts on the lattice nodes: bosses under the lid, nut pocket from below
boss_h     = 2;          // below the lid plate
lid_ext    = 4;          // lid reaches this far past the cover's seam walls, under its screw bosses
seam_screws_x = [150, 196];   // y positions on the vertical seam
seam_screws_y = [200, 232];   // x positions on the horizontal seam
// Lid ↔ cover screws from the cover's top, down into nuts pressed under the lid (6.4 between lid and cover top: no
// room for a side-slid nut pocket in the cover). Axis 1.5 outside the seam wall's centre: the lid's nut boss clears
// its edge.
seam_off   = -1.5;       // screw axis from the seam wall's centre, + = into the cover
cover_boss = 9;          // square boss on the seam wall, lid to cover top, head recessed from the top
seam_boss_d = 11;        // nut boss under the lid
head_bore = 6.2;         // M3 socket head Ø5.5 + play; 45° cone down to the clearance hole (no bridge in print)
cable_cover_x = 262;     // cover's front wall open from the tray wall to here, up to z_lid1 + 4: right ribbons

// --- cover ----------------------------------------------------------------------
// cover-bottom ↔ cover-top: nut tabs on the floor inside the walls, under the raised heatsink; screws from outside
bottom_tabs_front = [280, 330];   // x, through the front wall
bottom_tabs_right = [160, 215];   // y, through the right wall
bottom_tabs_back  = [310];        // x, through the back wall
bottom_tab_h = 10;               // above the floor: stays under the heatsink (hs_z0)
tab_t = 5.6;             // tab with a nut slot: 2.7 slot + ~1.45 each side
tab_w = 14;
cover_grid_x = [220, 260, 300, 330];   // cover lattice lines
cover_grid_y = [150, 190, 220];

// --- airflow --------------------------------------------------------------------
slot_w     = 3;          // grille slits
slot_pitch = 6;
vent_w     = 10;         // wall windows (material + air)
vent_pitch = 14;
vent_margin = 8;         // solid wall above / below the windows
