include <params.scad>

// Hexagon prism, flats on ±Y, `c2c` corner to corner.
module hex(c2c, h) { cylinder(d = c2c, h = h, $fn = 6); }

// One driver for a tip of `c2c` corner to corner, standing on the bed with the tip up.
module driver(c2c) {
    af = c2c * cos(30);                         // across flats
    cone_h = (shank_d - af) / 2;               // 45°
    difference() {
        union() {
            handle();
            translate([0, 0, handle_len]) cylinder(d1 = shank_d, d2 = af, h = cone_h);
            // tip starts inside the cone: one solid, no coincident faces
            translate([0, 0, handle_len]) tip(c2c, cone_h + tip_len);
        }
        label(str(c2c));
    }
}

module handle() {
    difference() {
        union() {
            cylinder(d1 = handle_d - 2 * bed_chamfer, d2 = handle_d, h = bed_chamfer);
            translate([0, 0, bed_chamfer]) cylinder(d = handle_d, h = handle_len - bed_chamfer - top_round);
            translate([0, 0, handle_len - top_round])
                cylinder(d1 = handle_d, d2 = handle_d - 2 * top_round, h = top_round);  // 45°: no overhang
        }
        for (i = [0 : flutes - 1])
            rotate(i * 360 / flutes)
                translate([handle_d / 2 + flute_d / 2 - flute_depth, 0, -1])
                    cylinder(d = flute_d, h = handle_len - top_round + 1);
    }
}

module tip(c2c, h) {
    hull() {
        hex(c2c, h - tip_chamfer);
        hex(c2c - 2 * tip_chamfer / cos(30), h);
    }
}

// Mirrored so it reads from below.
module label(s) {
    translate([0, 0, -1]) linear_extrude(label_depth + 1)
        mirror([1, 0, 0]) text(s, size = label_size, halign = "center", valign = "center",
                                font = "Liberation Sans:style=Bold");
}
