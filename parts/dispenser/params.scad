// Dispenser roller (belt conveyor under the left pile): all dimensions in mm.
// Frame = dispenser.stl's own coordinates, so parts drop straight into Dev's model:
//   X: left -> right. Left side open below z 70, divider at x 72..77, lift on the right.
//   Y: back wall (y 0) -> open front (y -165).   Z: up, box floor top at z 5.
// Cards lie with the long side along Y and travel along X: -X = take (out of the box),
// +X = feed (through the divider slot onto the lift).

$fn = 96;

// --- Dev's box (measured in dispenser.stl; change there, then here) ------------------
floor_top        = 5;
back_wall        = -5;       // inner face of the back wall
divider_x        = 72;       // left face of the divider
left_wall_bottom = 70;       // left wall ends here, open below
slot_top         = 66.04;    // underside of the divider's upper block = top of the card slot

// --- Card ------------------------------------------------------------------------------
card    = [63, 88];          // X (travel) x Y
card_yc = -98;               // card centre in Y (= lift plate centre)
card_t  = 0.32;              // ⚠️ measure: thickness of 20 cards / 20
gap     = 1.5 * card_t;      // gate opening above the belt: 1 card passes, 2 don't
belt_top = slot_top - gap;   // underside of the bottom card

// --- O-ring belts (NBR, bought) ---------------------------------------------------------
oring_id = 80;               // 80 x 3 mm
cord     = 3;
stretch  = 1.07;             // fitted length / free length: tension without a tensioner
cord_ys  = [for (o = [-20, 0, 20]) card_yc + o];
cord_s   = cord / sqrt(stretch);   // stretched cord gets thinner

// --- Drums (printed, run on 2x MR105ZZ each, on a fixed 5 mm steel rod) -------------------
groove_root = 5.5;           // groove bottom radius
groove_r    = cord / 2 + 0.1;
rim_r       = groove_root + 0.6 * cord_s;   // cord sits above its centre line in the groove
cord_c      = groove_root + cord_s / 2;     // cord centre line radius
cord_out    = groove_root + cord_s;         // top of the cord on the drum
bearing     = [5, 10, 4];    // MR105ZZ: id, od, width
press       = 0.1;           // bearing seat oversize (as arm/), tune with a test print
rod_d       = 5;             // steel rod (silver steel), fixed in the plates
rod_hole    = 5.2;           // slip fit in the plates
drum_bore   = 5.6;           // clearance between the bearings
boss        = 1;             // plate boss on the rod: touches only the inner race (MR105 ~7 mm)
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
motor_face_y  = back_wall - 0.5 - motor_body;   // shaft points to the front (-Y)
motor_dx      = 12;          // motor axis left of the drum axis: keeps the motor off the divider
motor_x       = drive_x - motor_dx;
motor_z       = drum_z - sqrt(gear_cd * gear_cd - motor_dx * motor_dx);

// --- Frame -----------------------------------------------------------------------------------
// Closed O-rings: nothing may run through a loop that is tied to both plates. So the front plate is
// a separate part, and inside the loops there is only what slides on from the front with them:
// drums, a flat bed (top = underside of the belts) and ribs along Y between the bed and the bottom run.
plate_t     = 6;
back_y      = [motor_face_y - plate_t, motor_face_y];   // back plate = motor plate
front_y     = [-152, -146];  // front plate (separate part)
drum_y      = [back_y[0] - boss, front_y[1] + boss];      // drum length along Y
gear_y      = [drum_y[0] - gear_w, drum_y[0]];
bed_top     = belt_top - cord_s;   // belts slide on it: a strand carries ~2.5 N, a pile would sag it
bed_t       = 3;
frame_x     = [idler_x - 10.5, divider_x - 0.5];
step_x      = -0.3;          // left of this the frame stands on the table (box floor starts at x 0)
rib_xs      = [-38, 3, 48];  // ribs under the bed (left faces), along Y; the right one clears drum and belts
rib_t       = 5;
cam_window  = [[10, 46], [-74, -56]];   // keep free: camera looks up at the bottom card
open_clear  = 0.6;           // around drums and gears in the bed

// --- Gates (printed, reprint with -D gate_gap=… to tune) ---------------------------------------
gate_gap  = gap;
gate_w    = 5;               // bar thickness in X; legs over the plates, pegs into the plates
gate_top  = left_wall_bottom - 0.4;   // left gate sits under the left wall
gate_xs   = [-gate_w, divider_x - 0.5 - gate_w];   // left gate bar x0, right gate bar x0
// right gate only: lip under the card path between the drive drum and the divider (the card's leading edge would
// droop into that 10 mm gap and hit the divider under the slot), joined to the bar at both card ends; there the slit
// closes in at 45° from the entry side to the card's width
lip_drop  = 0.5;             // lip top below belt_top, 0.06 above the divider under the slot (z 65): a card left
                             // lying on the lip sits below the next one's path, which slides over it, not into its edge
lip_edge  = 0.8;             // thinnest the lip gets at its drum-side edge
lip_clear = open_clear;      // around the belts on the drum
slit_play = 1;               // per side, beside the card at the slit's exit
screw_pilot = 2.5;           // M3 self-tapping into PETG
screw_clear = 3.4;
screw_head  = 6.2;           // counterbore

// --- Pusher: stack(B onto H) --------------------------------------------------------------------
// 17HS4401 on the right wall's outer face, coupling 5 -> 8, TR8x8 screw to the left wall. Carriage = the lift
// turned 90°: plate (push face) + bar along the front strip + nut tab. No rods: the plate rests on the lift (then the
// divider) by its own weight, and that weight keeps the nut from turning.
// Everything but the plate stays in the front strip (y < -146: no cards, no lift) or above the pile.
box_front  = -165;
div_top    = 96.04;          // top of the divider block (z 66..96, full depth)
right_wall = [145, 150];
left_wall  = [-5, 0];
riser_y    = -54;            // front face of the lift's riser: the cards' back stop
lift_top0  = 19;             // lift plate top as drawn in dispenser.stl (its lowest position)
pile_max   = 35;             // 30 loose cards, not pressed together
push_step  = 1;              // lift plate top above the divider top while pushing: cards step down onto it
push_lift  = div_top + push_step;      // lift plate top while pushing (firmware) = the plate's lower edge
push_rest  = 0.3;            // notch floor below push_lift: the plate starts level with the lift, never above it
push_h     = pile_max;
push_t     = 4;              // the notch is 4 deep: 1 mm of wall stays outside, the plate's home stop
push_y     = [-146.5, riser_y - 1];
push_cham  = [0.8, 1.5];     // lower edge, push side / return side: ramps over the lift's and the notch's edges
push_end   = divider_x;      // plate face at the end: plate on the divider, pile at x 9..72 over H's chute
push_stroke = right_wall[0] - push_end;

// bought: 17HS4401 (42 x 42 x 40), Purecrea flexible coupling 8/5, TR8x8 lead screw + brass nut
m44_sq     = 42.3;
m44_body   = 40;
m44_boss   = [22, 2];
m44_shaft  = [5, 24];        // from the face
m44_holes  = 31;
coupling   = [19, 25];       // diameter, length (half on the shaft, half on the screw)
nut_d      = 10.2;           // TR8 brass nut: body, length incl. flange, flange, screw circle
nut_len    = 15;
nut_flange = [22, 3.5];
nut_pcd    = 16;
lead       = 8;

// layout: screw axis in the front strip, high enough that the motor's lower screw holes clear the plate's notch
pu_y       = -156.5;
pu_screw_z = push_lift + (pile_max + 22);
m44_face   = right_wall[1];
shaft_tip  = m44_face - m44_shaft[1];
coup_x     = [shaft_tip - coupling[1] / 2, shaft_tip + coupling[1] / 2];
bar_h      = 10;
bar_y      = [box_front, push_y[0]];
tab_t      = 5;
nut_x      = [coup_x[0] - 1.5 - nut_len, coup_x[0] - 1.5];   // at home; flange on the right
tab_x      = [nut_x[1] - nut_flange[1] - tab_t, nut_x[1] - nut_flange[1]];
bar_x      = [tab_x[0], right_wall[0] + push_t];
screw_x    = [left_wall[0] + 1, shaft_tip];   // cut the screw to this length

// Dev's changes to the box: notch for the carriage at home (open to the front, 4 deep), the wall extended forward
// around the motor face, the screw's hole in the left wall
notch_lo = [[box_front - 1, riser_y], [push_lift - push_rest, push_lift + (bar_h + 0.5)]];
notch_hi = [[push_y[0] - 0.5, riser_y], [push_lift - push_rest, push_lift + (push_h + 1)]];
ext_y    = [pu_y - m44_sq / 2 - 2, box_front];
ext_z    = [pu_screw_z - m44_sq / 2 - 2, pu_screw_z + m44_sq / 2 + 2];
screw_hole = 8.6;

// --- Lift motor (Dev's lift, moved for the coupling) ----------------------------------------------
// As drawn, the coupling would sit where the lift's nut is at the bottom (z 47.5). The motor goes under the floor,
// face against its underside: coupling z 11.5..36.5, the lift keeps its full travel.
lift_axis  = [111.9, -26];
floor_t    = 5;
lift_face  = 0;
lift_screw_top = 200;     // as drawn (201.2), ending at the box top
