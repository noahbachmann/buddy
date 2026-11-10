// Dispenser roller (belt conveyor under the left pile): all dimensions in mm.
// Frame = dispenser.stl's own coordinates, so parts drop straight into Dev's model:
//   X: left -> right. Left side open below z 70, divider at x 72..77, lift on the right.
//   Y: back wall (y 0) -> open front (y -165 as drawn, cut back by `shift`).   Z: up, box floor top at z 5.
// Cards lie with the long side along Y and travel along X: -X = take (out of the box),
// +X = feed (through the divider slot onto the lift).

$fn = 96;

// --- Dev's box (measured in dispenser.stl; change there, then here) ------------------
floor_top        = 5;
back_wall        = -5;       // inner face of the back wall
wall_t           = 3;        // case walls and floor, thinned from Dev's 5 at their outer faces
back_out         = back_wall + wall_t;   // back wall's outer face: the case prints on it (case.scad)
floor_bot        = floor_top - wall_t;   // floor's underside
divider_x        = 72;       // left face of the divider
left_wall_bottom = 70;       // left wall ends here, open below
slot_top         = 66.04;    // underside of the divider's upper block = top of the card slot
div_low          = 65;       // top of the divider's lower part (the case cuts it lower under the slot: slot_bot)
divider_t        = 3;        // thinned from Dev's 5 at its right face: shorter slot; gates and roller stay put
divider_x1_dev   = 77;       // the divider's right face as drawn
b_shift          = divider_x1_dev - (divider_x + divider_t);   // everything right of the divider (pile B, lift, right
                             // wall, pusher) moves left by what the divider lost: B's width stays Dev's
// The roller's back plate and the lift's screw sit `shift` further back than in dispenser.stl (4023 in the back wall,
// lift nut flange 0.9 mm from the wall).
shift            = 8.5;

thin             = 3;       // printed walls, plates, tabs: 3 max unless the part takes real load (Dev)

// --- Card ------------------------------------------------------------------------------
card    = [66.5, 91];        // X (travel) x Y, sleeved
card_x0 = 0.5;               // card's left edge on the roller: 0.5 from the left gate
card_yc = -85.9;             // card centre in Y: back edge (y -40.4) = the foot of the lift's slope, as far back as its nut allows (lib asserts)
box_front = card_yc - card[1] / 2 - 23;   // open front edge, 23 in front of the cards (pusher's strip) as drawn
slot_y    = [box_front, back_wall];       // the divider slot runs the full depth: open front .. back wall
card_t  = 0.5;               // sleeved, pressed flat (Dev: < 0.5 pressed, > 1 loose: bow and air in the sleeve). Upper
                             // bound: the gates and the pile flatten the card. ⚠️ exact value: 10 cards pressed / 10
gap     = 0.55;              // gate opening above the belt (Dev): 1 pressed card (<= 0.5) passes, 2 do not
belt_top = slot_top - gap;   // underside of the bottom card

// --- O-ring belts (NBR, bought) ---------------------------------------------------------
oring_id = 80;               // 80 x 3 mm
cord     = 3;
stretch  = 1.07;             // fitted length / free length: tension without a tensioner
cord_ys  = [for (o = [-20, 0, 20]) card_yc + o];
cord_s   = cord / sqrt(stretch);   // stretched cord gets thinner

// --- Drums (printed solid, run on 2x 685ZZ each, one fixed steel pin per bearing) --------
groove_root = 5.5;           // groove bottom radius
groove_r    = cord / 2 + 0.1;
rim_r       = groove_root + 0.6 * cord_s;   // cord sits above its centre line in the groove
cord_c      = groove_root + cord_s / 2;     // cord centre line radius
cord_out    = groove_root + cord_s;         // top of the cord on the drum
bearing     = [5, 11, 5];    // 685ZZ: id, od, width (#59; idler end wall 1.7)
press       = 0.1;           // bearing seat oversize (as arm/), tune with a test print
pin         = [5, 20];       // dowel pin ISO 8734 (RS PRO 5 x 20), bought: no cutting (#57, #60)
pin_hole    = 5.2;           // slip fit in the back plate (front plate: pin_press_d)
pin_pocket  = [5.6, 1];      // drum: pocket behind each bearing for the pin tip (d, clearance past the tip)
boss        = 1;             // plate boss on the pin: touches only the inner race (685 ~7 mm)
boss_d      = 7;

// --- Gears (printed, module 1) -------------------------------------------------------------
gear_mod      = 1;
drum_teeth    = 16;          // on the drive drum; limited by the divider
motor_teeth   = 20;
gear_w        = 7;
gear_backlash = 0.1;
gear_slack    = 0.1;         // extra centre distance for printed teeth
gear_cd       = gear_mod * (drum_teeth + motor_teeth) / 2 + gear_slack;

// --- Layout ---------------------------------------------------------------------------------
drum_z   = belt_top - cord_out;            // both drum axes
drive_x  = divider_x - 1 - gear_mod * (drum_teeth + 2) / 2;   // 1 mm from the divider
belt_c   = (stretch * PI * (oring_id + cord) - 2 * PI * cord_c) / 2;   // drum centre distance
idler_x  = drive_x - belt_c;

// 4023 (NEMA 17 pancake, 42.3 square, 23.5 body, 22 mm boss 1.9, 5 mm D-shaft to 25.1)
motor_sq      = 42.3;
motor_body    = 23.5;
motor_boss    = [22, 1.9];
motor_holes   = 31;          // M3 square
motor_face_y  = back_wall - 0.5 - motor_body + shift;   // shaft to the front (-Y), connector down; body through
                                                        // a window in the back wall, resting on the hold below
motor_dx      = 12;          // motor axis left of the drum axis: keeps the motor off the divider
motor_x       = drive_x - motor_dx;
motor_z       = drum_z - sqrt(gear_cd * gear_cd - motor_dx * motor_dx);

// --- Frame -----------------------------------------------------------------------------------
// Closed O-rings: nothing may run through a loop that is tied to both plates. So the front plate is
// a separate part, and inside the loops there is only what slides on from the front with them:
// drums, a flat bed (top = underside of the belts) and ribs along Y between the bed and the bottom run.
plate_t     = 6;
back_y      = [motor_face_y - plate_t, motor_face_y];   // back plate = motor plate
front_y     = [card_yc - card[1] / 2 - 4 - plate_t, card_yc - card[1] / 2 - 4];   // front plate (separate part), 4 in front of the cards
drum_y      = [back_y[0] - boss, front_y[1] + boss];      // drum length along Y
gear_y      = [drum_y[0] - gear_w, drum_y[0]];
bed_top     = belt_top - cord_s;   // belts slide on it: a strand carries ~2.5 N, a pile would sag it
bed_t       = 3;
frame_x     = [idler_x - 10.5, divider_x - 0.5];
step_x      = -0.3;          // left of this the frame stands on the table (box floor starts at x 0)
rib_xs      = [-38, 3, 48];  // ribs under the bed (left faces), along Y; the right one clears drum and belts
rib_t       = thin;
rib_boss    = [6, 11];       // d, length: around the front plate's screw at each rib's front end (teardrop, merges into the bed)
cam_window  = [[10, 46], [card_yc + 24, card_yc + 42]];   // keep free: camera looks up at the bottom card
open_clear  = 0.6;           // around drums and gears in the bed

// --- Gates (printed, reprint with -D gate_gap=… to tune) ---------------------------------------
gate_gap  = gap;
gate_w    = 5;               // bar thickness in X; legs over the plates, pegs into the plates
gate_top  = left_wall_bottom - 0.4;   // left gate sits under the left wall
gate_xs   = [-gate_w];        // left gate bar x0 (printed); the right gate is part of the box
gate_in   = card_x0 + card[0] + 0.5;   // right gate's inner face (box), 0.5 beside the card
// right gate only: lip under the card path between the drive drum and the divider (the card's leading edge would
// droop into that 10 mm gap and hit the divider under the slot), joined to the bar beside the card.
// Slit ends (gate + divider slot, Dev's change): one 45° funnel, card width + slit_play per side at the divider's right
// face, wider towards the gate's entry side: an offset card is led to the middle
lip_drop  = card_t + 0.2;    // lip top below belt_top: a card left lying on the lip sits below the next one's path,
                             // which slides over it, not into its edge
lip_top   = belt_top - lip_drop;
slot_bot  = lip_top - 0.06;  // the divider's lower part, cut down under the slot to just below the lip
lip_edge  = 0.8;             // thinnest the lip gets at its drum-side edge
lip_clear = open_clear;      // around the drum gear's tips: the drums slide onto their pins under the lip
slit_play = 1;               // per side, beside the card at the slit's exit
screw_pilot = 2.5;           // M3 self-tapping into PETG
screw_clear = 3.4;
screw_head  = 6.2;           // counterbore

// --- Pusher: stack(B onto H) --------------------------------------------------------------------
// 17HS4401 on the right wall's outer face, coupling 5 -> 8, TR8x8 screw to the left wall. Carriage = the lift
// turned 90°: plate (push face) + bar along the front strip + nut tab. No rods: the plate rests on the lift (then the
// divider) by its own weight, and that weight keeps the nut from turning.
// Everything but the plate stays in the front strip (in front of the cards and the lift) or above the pile.
div_top    = 96.04;          // top of the divider block (z 66..96, full depth)
right_wall = [145, 150] - [b_shift, b_shift];
left_wall  = [-5, 0];
riser_y    = card_yc + card[1] / 2;   // front face of the lift's back stop
pile_max   = 35;             // 30 loose cards, not pressed together
push_step  = 1;              // lift plate top above the divider top while pushing: cards step down onto it
push_lift  = div_top + push_step;      // lift plate top while pushing (firmware) = the plate's lower edge
push_rest  = 0.3;            // notch floor below push_lift: the plate starts level with the lift, never above it
push_h     = pile_max;
push_t     = thin;           // the notch is 3 deep, through the 3 mm wall: a skin behind it is the plate's home stop
notch_skin = 2;              // behind the notch, outside the wall; also its rim above, where the motor sits on it
notch_play = 0.5;            // notch above the plate's top
push_y     = [card_yc - card[1] / 2 - 4.5, riser_y - 1];   // 0.5 over the lift plate's front edge
push_cham  = [0.8, 1.5];     // lower edge, push side / return side: ramps over the lift's and the notch's edges
push_end   = divider_x;      // plate face at the end: plate on the divider, pile at x 9..72 over H's chute
push_stroke = right_wall[0] - push_end;

// bought: 17HS4401 (42 x 42 x 40), Purecrea flexible coupling 8/5, TR8x8 lead screw. Nut printed (tr8-nut.scad):
// the brass nut's flange and bolt circle, flange at one end (no spigot), body 11 (brass 10.2: only 0.9 of PETG
// would be left under the thread root, 11 leaves 1.3)
m44_sq     = 42.3;
m44_body   = 40;
m44_boss   = [22, 2];
m44_shaft  = [5, 24];        // from the face
m44_holes  = 31;
coupling   = [19, 25];       // diameter, length (half on the shaft, half on the screw)
nut_d      = 11;             // TR8 nut: body, length incl. flange, flange, screw circle
nut_len    = 15;
nut_flange = [22, 3.5];
nut_pcd    = 16;
nut_starts = 4;              // TR8x8: pitch 2, 4 starts = 8 per turn
nut_slop   = 0.2;            // thread gap per side, PETG on the steel screw. ⚠️ tune: binds → up, wobbles → down
lead       = 8;

// layout: screw axis in the front strip; the motor sits on the notch's skin, above the plate
pu_y       = box_front + 8.5;
pu_screw_z = push_lift + (push_h + notch_play) + notch_skin + m44_sq / 2;   // motor sits on the notch's skin
m44_face   = right_wall[0] + wall_t;   // on the thinned wall's outer face
shaft_tip  = m44_face - m44_shaft[1];
coup_x     = [shaft_tip - coupling[1] / 2, shaft_tip + coupling[1] / 2];
bar_h      = 10;
tab_t      = thin;           // nut tabs (pusher and lift); pushing / the lift's weight press the flange onto them
nut_x      = [coup_x[0] - 1.5 - nut_len, coup_x[0] - 1.5];   // at home; flange on the right
tab_x      = [nut_x[1] - nut_flange[1] - tab_t, nut_x[1] - nut_flange[1]];
bar_x      = [tab_x[0], right_wall[0] + push_t];
lead_len   = 200;            // both screws: stock length, not cut (Dev can't cut metal), end in the coupling at the shaft tip
screw_x    = [shaft_tip - lead_len, shaft_tip];   // turns in the left wall's hole, sticks out of the box's left side

// Guide against turning: a ridge along X on the front strip's tops (border H, divider, border B) runs in a groove in the
// bar's underside. Turning the plate's far end up moves the bar (57 below the screw) back (+Y): the ridge's front face
// stops it. Free in Z: the plate still rests on the lift by its weight.
guide_yf    = pu_y - 4.5;    // ridge's front face (the contact face)
guide_h     = 3;             // above div_top: 2 into the groove with the bar on the lift
guide_top   = 1;             // flat top; back face 45° (the case prints on its back, -Y up)
guide_taper = [0.6, 2];      // front face set back at each segment's ends (depth, length): the groove passes the
                             // borders' 0.2 play at the joints
groove_play = [0.2, 1.2];    // front (contact), back: stepping onto the divider tilts the bar ~0.6 forward
groove_d    = guide_h + 0.5; // the bar still clears the ridge if it sits 1 lower (on the divider)
bar_y      = [guide_yf - groove_play[0] - thin, push_y[0]];   // front leg `thin` in front of the groove

// Dev's changes to the box: notch for the carriage at home (open to the front, through the wall), the wall extended forward
// around the motor face, the screw's hole in the left wall
notch_lo = [[box_front - 1, riser_y], [push_lift - push_rest, push_lift + (bar_h + 0.5)]];
notch_hi = [[push_y[0] - 0.5, riser_y], [push_lift - push_rest, push_lift + (push_h + notch_play)]];
ext_y    = [pu_y - m44_sq / 2 - 2, box_front];
ext_z    = [pu_screw_z - m44_sq / 2 - 1.5, pu_screw_z + m44_sq / 2 + 2];   // 1.5 under: 2 would land on the notch top (slivers)
box_top  = ext_z[1];         // Dev's box (z 200) cut down to the pusher motor's wall extension: nothing above it
screw_hole = 8.6;

// --- Lift (printed, replaces Dev's) ----------------------------------------------------------------
// Motor under the floor, face against its underside: coupling z 11.5..36.5. Side view: the nut tab around the screw at
// the top, from its front edge one slope (leaning ~10°) down to the plate; the front border (box) leans the same way, so
// a card dropping onto a low lift slides into place. Seen from the front the slope narrows upwards along straight
// sides. The nut connection is Dev's as drawn but 3 thick (his: 5): tab with a round end, Ø10.2 for the screw, the
// printed nut's flange under it with 4 pegs up into the tab on a 16 mm square (#63). The plate's edges against the divider and the right wall keep it from
// turning.
lift_axis  = [111.8 - b_shift, -25.4 + shift];   // Dev's nut and tab centre (his screw was drawn 0.6 off)
lift_face  = floor_bot;      // motor face against the floor's underside
lift_screw_top = lift_face + m44_shaft[1] + lead_len;   // sticks out above the box top
lift_x     = [divider_x + divider_t + 0.44, right_wall[0] - 0.56];   // plate: 0.44 to the divider, 0.56 to the right wall (as Dev's)
lift_t     = thin;           // plate thickness
lift_front = card_yc - card[1] / 2 - 4;   // plate's front edge: plate top 92 long to the slope's foot (Dev's)
stop_t     = thin;           // slope wall, measured along Y, leaning like its front face
stop_h     = 47;             // plate top .. tab top (Dev's)
stop_lean  = 8;              // the slope's front face leans back this much over stop_h (~10°)
stop_w     = 28;             // at the top (Dev's tab width), centred on the screw
lift_tab_r = 10.5;           // Dev's tab: round end around the nut
lift_holes = [10.2, 8];      // Dev's tab: centre hole (screw passes), the nut's 4 peg holes at ±8 in X and Y
lift_min   = (belt_top - lip_drop) - pile_max - 0.5;   // lowest plate top: a full pile under the slot
lift_tab_y = lift_axis[1] + lift_tab_r;   // tab's back edge (Dev's), 1.4 to the back wall
border_t   = 4;              // front border of pile B (box) at the top, floor .. div_low
h_bord_drop = 0.4;           // pile H's borders: bottom this far under the belt top, so the bottom card hits them

// --- Holds: the motors rest on the case, not only on their screws ------------------------------------------
m23_conn   = [16, 4, 7.6];   // 4023 connector: width, out of the body, along the axis from the back end
m23_window = 0.3;            // clearance around the 4023 in the back wall's window
cable_slot = [20, 2];        // channel under the connector down through the floor: width, clearance in Y
hold_gap   = 0.3;            // between the roller hold and the frame's back plate
pu_hold    = [16, 3];        // pusher motor hold: width (Y), top thickness; 45° underside

// --- Case print (case.scad): one piece on its back, borders in front of the piles as slide-in parts ---------------
feet_h     = m44_body + 2;   // rails under the floor, left and right edge: the lift motor hangs 40 below it
feet_t     = thin;
insert_fit = 0.2;            // per side, border inserts against walls, divider and gate
insert_screws_h = [[-146, 78], [-146, 89]];   // border H: (y, z), printed pin through the left wall
insert_screws_b = [[95 - b_shift, -146], [127 - b_shift, -146]];  // border B: (x, y), printed pin from below through the floor

// --- Case spares (case-spares.scad): the case minus material nothing needs, still no supports ---------------------
spare_top   = notch_hi[1][1] + wall_t;     // behind the pusher motor the walls end here: 3 over the notch, above any card
spare_y     = pu_y + m44_sq / 2 + wall_t;  // full height in front of this (pusher motor face + 3), 45° down behind it
spare_rim   = 6;             // frame left around each window
spare_tab   = 3;             // back wall kept solid this far around the lift tab's path (tab 1.4 off the wall)
foot_chords = [4, 3];        // feet truss: bottom chord, top chord under the floor (+ half a strut each)
foot_strut  = 5;             // feet truss: diagonal width

// --- Printed joints instead of screws (#63) ------------------------------------------------------------------
// Fits per side, hole vs. a printed Ø peg_d peg. "vertical" / "horizontal" = the hole's axis in its own part's print.
// From the fit test (fit-test.scad, plate 0, PETG 0.2 mm, 2026-11-09). Vertical holes print smaller than horizontal.
peg_d       = 3;             // every printed peg and pin shank
fit_steps   = [-0.05, 0, 0.05, 0.1, 0.15, 0.2];   // fit test: hole 1..6 (per side), from the chamfered end
pin_steps   = [4.85, 4.9, 4.95, 5.0, 5.05, 5.1];  // fit test: holes for the Ø5 steel pin (diameters)
fit_slip    = 0.1;           // vertical hole: peg slides in without play (fit test: hole 4)
fit_press   = 0.05;          // vertical hole: peg pushed in by hand, holds (hole 3)
fit_slip_h  = 0.05;          // horizontal (teardrop) hole: slip (hole 3)
fit_press_h = 0;             // horizontal hole: press (hole 2)
pin_press_d = 4.95;          // front plate: Ø5 steel pin pushed in by hand (firm), stays in when the plate comes off. ⚠️ not tested yet
peg_cham    = 0.4;           // lead-in at every peg tip
nut_peg_h   = tab_t - 0.3;   // nut pegs: into the tab, not through it
plate_peg_h = 5;             // front plate pegs into the rib ends
bpin_head   = [6, 1.2];      // border pin: head d, h
bpin_depth  = 6;             // shank in the border (beyond the case's wall or floor)
