// Buddy arm: all dimensions in mm. Change values here, not in the part files.
// Frame of every joint: pivot axis = Y, limbs run along Z, X = front/back.

$fn = 96;

// --- Human proportions (average adult) -------------------------------------
upper_arm_len = 300;  // shoulder pivot -> elbow pivot
forearm_len   = 260;  // elbow pivot -> wrist face

// Outer tube profiles: [z, half depth X (front/back), half width Y (side/side)].
// Upper arm: z measured up from the elbow pivot. Forearm: z down from the elbow pivot (negative).
upper_arm_profile = [[62, 34, 30], [150, 45, 41], [240, 44, 40]];
forearm_profile   = [[-255, 30, 21], [-150, 35, 31], [-60, 41, 36]];

wall = 2.4;  // tube wall (= 6 perimeters at 0.4 mm)

// --- Print splits (P2S bed: 256 x 256 x 256) --------------------------------
ua_split  = 150;   // upper arm cut, z from elbow pivot
fa_split  = -140;  // forearm cut, z from elbow pivot
sock_len  = 16;    // coupler depth per side
sock_wall = 2.0;   // min tube wall around the coupler
neck      = 2;     // ledge the coupler stops against
coupler_fit  = 0.2;
coupler_wall = 2.4;

// --- Servo (standard size, 25T spline, e.g. DS3218 / DS3235) ----------------
// ⚠️ Typical values. Measure your servos and correct these before printing.
servo_body       = [40.5, 20.2];  // length (along the limb), width
servo_below      = 27.0;          // bottom face -> underside of mounting flange
servo_flange_len = 54.0;
servo_hole_pitch = [49.5, 10.0];  // flange holes: along length, across width
servo_shaft_off  = 10.2;          // shaft axis -> near end of body
servo_horn_face  = 18.0;          // flange underside -> outer face of the mounted disc horn
horn_pcd         = 18.0;          // 4x M3 holes in the aluminium disc horn
servo_screw_d    = 2.5;           // pilot hole for M3 self-tapping screws
fit = 0.3;                        // clearance per side around the servo body

// --- Bearing & axle (idler side of every joint) ------------------------------
bearing   = [8, 22, 7];  // 608 skate bearing: id, od, width
press     = 0.1;         // bearing seat oversize, tune with a test print
bolt_d    = 8.4;         // M8 x 20 bolt
nut_af    = 13;          // M8 nut across flats
nut_t     = 6.5;
boss_d    = 11;          // presses only on the bearing's inner race (od ~12)
lip_hole_d = 17.5;       // outer lip: holds the outer race, clears 2x M8 washers (od 16)
idler_lip = 1.5;

// --- Joint geometry -----------------------------------------------------------
knuckle_r  = 26;   // round end of the servo housing
stem_half  = 14;   // knuckle half depth above the pivot (narrow = more elbow range)
knuckle_len = 42;  // straight knuckle before the tube starts (servo slides in below this)
joint_gap  = 1.0;  // knuckle face <-> idler cheek
bearing_recess = 1.0;
horn_gap   = 0.2;
horn_cheek_t = 6;
cheek_r    = knuckle_r;
fork_half_x = 28;
fork_bridge_z = knuckle_r + 3;
fork_bridge_t = 6;
cap_small  = 8;    // void tapers to this half size under a fork (45° roof, no supports)

pocket_floor_gap = 1.0;
inner_wall = 1.6;  // between servo pocket and nut slot
outer_wall = 2.0;  // between nut slot and idler face
nut_slot_t = nut_t + 0.3;
channel_h  = 14;   // cable channel depth (Y), stays clear of the flange screws

// --- Shoulder mount -------------------------------------------------------------
mount_stem = 50;            // pivot -> underside of the mounting plate
plate      = [90, 80, 6];   // X, Y, thickness
plate_hole_d = 4.5;         // M4
plate_holes  = [70, 56];    // hole pattern X, Y

// --- Wrist plate (hand interface, placeholder until 3.1) ----------------------
wrist_t      = 5;
wrist_hole_d = 20;          // tendons / cables
wrist_screws = [32, 20];    // 4x M3 pattern X, Y

cable_hole_z = 215;  // elbow servo cable exits the upper arm here (back side)
cable_hole_d = 12;

// --- Derived (don't edit) -------------------------------------------------------
knuckle_w   = servo_below + pocket_floor_gap + inner_wall + nut_slot_t + outer_wall;
y_flange    = knuckle_w / 2;       // servo flange seat (+Y face of the knuckle)
y_back      = -knuckle_w / 2;      // idler face
y_horn      = y_flange + servo_horn_face;
pocket_y0   = y_flange - servo_below - pocket_floor_gap;
boss_h      = joint_gap + bearing_recess;
idler_cheek_t = bearing_recess + bearing[2] + idler_lip;
idler_in    = y_back - joint_gap;
fork_y0     = idler_in - idler_cheek_t;
horn_cheek_y = y_horn + horn_gap;
fork_y1     = horn_cheek_y + horn_cheek_t;
yc          = (fork_y0 + fork_y1) / 2;  // limb centre line (Y)
body_c      = servo_body[0] / 2 - servo_shaft_off;  // servo body centre, z from shaft
