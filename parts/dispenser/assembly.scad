// Roller + pusher in Dev's box. Preview only (F5). part = one placed piece for make_copy.py and the checks.
// Poses: push = 0 (home) .. 1 (end of stroke), lift_top = lift plate top (push_lift while pushing).
include <lib.scad>

part = "all";
push = 0;
lift_top = lift_top0;

module piece(name) {
  if (name == "frame")           frame();
  else if (name == "front-plate") front_plate();
  else if (name == "drive-drum") drum_tf(drive_x) drum(true);
  else if (name == "idler-drum") drum_tf(idler_x) drum(false);
  else if (name == "motor-gear") motor_tf() motor_gear();
  else if (name == "gate-left")  gate_placed(0);
  else if (name == "gate-right") gate_placed(1);
  else if (name == "motor")      motor_4023();
  else if (name == "rods")       rods();
  else if (name == "belts")      belts();
  else if (name == "pusher")     pusher(push);
  else if (name == "case-add")   difference() { case_add(); box_cuts(); }
  else if (name == "pusher-motor") m4401();
  else if (name == "pusher-coupling") pu_coupling();
  else if (name == "pusher-screw") pu_screw();
  else if (name == "lift-motor") lift_motor();
  else if (name == "lift-coupling") lift_coupling();
  else if (name == "lift-screw") lift_screw();
  else if (name == "pusher-nut") pu_nut(push);
  else if (name == "box-cut")    difference() { union() { box_trimmed(); case_add(); } box_cuts(); }
  else if (name == "lift")       translate([0, shift, lift_top - lift_top0]) import("ref/lift.stl");   // moved back
}

if (part != "all") piece(part);
else {
  %piece("box-cut");
  %piece("lift");
  color("orange") { piece("frame"); piece("front-plate"); }
  color("teal") { piece("drive-drum"); piece("idler-drum"); piece("motor-gear"); }
  color("gold") { piece("gate-left"); piece("gate-right"); }
  color("dimgray") piece("motor");
  color("silver") piece("rods");
  color("black") piece("belts");
  color("tomato") piece("pusher");
  color("lightsteelblue") piece("case-add");   // wall extension
  color("dimgray") piece("pusher-motor");
  color("silver") { piece("pusher-screw"); piece("pusher-coupling"); piece("lift-screw"); piece("lift-coupling"); }
  color("dimgray") piece("lift-motor");
  color("goldenrod") piece("pusher-nut");
  // card on the belts, centred between the gates
  color("white", 0.6) translate([(divider_x - card[0]) / 2 - 2, card_yc - card[1] / 2, belt_top]) cube([card[0], card[1], card_t]);
  // pile B on the lift, squared against the right wall, full height
  color("white", 0.6) translate([right_wall[0] - card[0], card_yc - card[1] / 2, lift_top]) cube([card[0], card[1], pile_max]);
}
