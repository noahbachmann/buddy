// Roller in Dev's box. Preview only (F5). part = one placed piece for make_copy.py.
include <lib.scad>

part = "all";

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
}

if (part != "all") piece(part);
else {
  %import("ref/box.stl");
  %import("ref/lift.stl");
  color("orange") { piece("frame"); piece("front-plate"); }
  color("teal") { piece("drive-drum"); piece("idler-drum"); piece("motor-gear"); }
  color("gold") { piece("gate-left"); piece("gate-right"); }
  color("dimgray") piece("motor");
  color("silver") piece("rods");
  color("black") piece("belts");
  // card on the belts, centred between the gates
  color("white", 0.6) translate([(divider_x - card[0]) / 2 - 2, card_yc - card[1] / 2, belt_top]) cube([card[0], card[1], card_t]);
}
