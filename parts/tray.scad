// Desiccant tray: box with vent windows and a divider, slotted lid.
// Render: openscad -D 'part="box"' -o tray-box.stl tray.scad   (part = "box" | "lid")
// Print both as rendered (flat side down), PETG, no supports.

part = "box";

L = 100;      // length (x)
W = 65;       // width (y)
H = 15;       // box height
wall = 1.6;   // walls, floor, lid plate

// Vent windows in the box walls: 1.2 wide, every 2.8, from z 2.6 to 10.
win_w = 1.2;
pitch = 2.8;
win_z0 = 2.6;
win_z1 = 10;
win_margin = 7;      // inner wall → first window
div_w = 1.6;         // divider in the middle (x)
div_gap = 1.75;      // divider end → inner wall (room for the lid lip)
div_clear = 2.4;     // no windows within this of the divider

// Lid: lip that drops into the box, slots for air.
lip_gap = 0.15;      // per side, lip → box inner wall
lip_t = 1.2;
lip_h = 4;
slot_w = 1.2;
slot_margin = 4.95;  // lid edge → slot ends (y)
slot_x0 = 4.6;       // first slot (x), mirrored on the other half
slot_n = 15;         // slots per half

eps = 0.01;

// Windows centred along a wall of inner length len.
function win_count(len) = floor((len - 2 * win_margin + (pitch - win_w)) / pitch + 1e-6);
function win_start(len) = (len - (win_count(len) * pitch - (pitch - win_w))) / 2;

module box() {
  difference() {
    cube([L, W, H]);
    translate([wall, wall, wall]) cube([L - 2 * wall, W - 2 * wall, H]);
    // long walls (y = 0 and y = W), windows along x
    for (i = [0 : win_count(L - 2 * wall) - 1]) {
      x = wall + win_start(L - 2 * wall) + i * pitch;
      if (x + win_w < L / 2 - div_w / 2 - div_clear || x > L / 2 + div_w / 2 + div_clear)
        translate([x, -eps, win_z0]) cube([win_w, W + 2 * eps, win_z1 - win_z0]);
    }
    // short walls (x = 0 and x = L), windows along y
    for (i = [0 : win_count(W - 2 * wall) - 1])
      translate([-eps, wall + win_start(W - 2 * wall) + i * pitch, win_z0])
        cube([L + 2 * eps, win_w, win_z1 - win_z0]);
  }
  // divider
  translate([L / 2 - div_w / 2, wall + div_gap, 0])
    cube([div_w, W - 2 * (wall + div_gap), H]);
}

module lid() {
  lip_o = wall + lip_gap;  // lip outer edge from the lid edge
  difference() {
    cube([L, W, wall]);
    for (i = [0 : slot_n - 1], x = [slot_x0 + i * pitch, L - slot_x0 - slot_w - i * pitch])
      translate([x, slot_margin, -eps]) cube([slot_w, W - 2 * slot_margin, wall + 2 * eps]);
  }
  translate([lip_o, lip_o, wall]) difference() {
    cube([L - 2 * lip_o, W - 2 * lip_o, lip_h]);
    translate([lip_t, lip_t, -eps]) cube([L - 2 * lip_o - 2 * lip_t, W - 2 * lip_o - 2 * lip_t, lip_h + 2 * eps]);
  }
}

if (part == "box") box();
else lid();
