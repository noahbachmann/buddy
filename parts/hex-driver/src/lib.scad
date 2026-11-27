include <params.scad>

// L-shaped key for a tip of `c2c` corner to corner, lying flat. Hex at both ends: short arm in the screw = lever
// (more torque than the printed hex survives, see README), long arm in the screw = reach and quick spinning.
module driver(c2c) {
    eps = 0.01;  // arms reach into the bend: no coincident faces
    difference() {
        union() {
            rotate(180) rotate_extrude(angle = 90) translate([bend_r, 0]) bar_profile();
            translate([-bend_r, -eps, 0]) arm(long_len + eps, c2c);
            translate([-eps, -bend_r, 0]) rotate(-90) arm(short_len + eps, c2c);
        }
        translate([-bend_r, long_len / 2, bar_h - label_depth])
            linear_extrude(label_depth + 1)
                rotate(90) text(str(c2c), size = label_size, halign = "center", valign = "center",
                                font = "Liberation Sans:style=Bold");
    }
}

// Bar cross-section: x across the bar, y = height. 45° bed chamfer, rounded top.
module bar_profile() {
    hull() {
        translate([-bar_w / 2 + bed_chamfer, 0]) square([bar_w - 2 * bed_chamfer, 0.01]);
        translate([-bar_w / 2, bed_chamfer]) square([bar_w, 0.01]);
        for (s = [-1, 1]) translate([s * (bar_w / 2 - top_round), bar_h - top_round]) circle(r = top_round);
    }
}

// Straight arm along +Y from y = 0: bar, 45° transition down to the hex, hex tip. Everything sits on z = 0.
module arm(len, c2c) {
    af = c2c * cos(30);
    trans = bar_h - af;  // top falls 45°
    rotate([90, 0, 0]) translate([0, 0, -len]) linear_extrude(len) bar_profile();
    hull() {
        translate([0, len, 0]) rotate([90, 0, 0]) translate([0, 0, -0.01]) linear_extrude(0.01) bar_profile();
        translate([0, len + trans, 0]) hex_y(c2c, 0.01);
    }
    translate([0, len + trans - 0.01, 0]) tip(c2c, tip_len + 0.01);
}

// Hex prism along +Y from y = 0, a flat on the bed (z = 0), `c2c` corner to corner.
module hex_y(c2c, h) {
    translate([0, 0, c2c * cos(30) / 2]) rotate([-90, 0, 0]) cylinder(d = c2c, h = h, $fn = 6);
}

// Chamfered end shrinks towards the axis, so its bottom face lifts off the bed: 0.3 mm at ~60°, prints fine.
module tip(c2c, h) {
    hull() {
        hex_y(c2c, h - tip_chamfer);
        translate([0, h - tip_chamfer, (c2c - (c2c - 2 * tip_chamfer / cos(30))) * cos(30) / 2])
            hex_y(c2c - 2 * tip_chamfer / cos(30), tip_chamfer);
    }
}
