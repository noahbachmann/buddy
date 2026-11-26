// Hook test: one front-edge hook with a strip of the lid's front band, cut from the real lid (same geometry, same
// print orientation). Snap it over the tray's front edge to check reach, hook size and play before the full lid.
include <lib.scad>
ht_margin = 10;          // band kept each side of the hook
ht_depth  = perim;       // band kept from the front edge in (the lid's solid rim)
flip_lid() intersection() {
  lid();
  translate([hook_xs[0] - hook_w / 2 - ht_margin, -big, -big]) cube([hook_w + 2 * ht_margin, big + ht_depth, 2 * big]);
}
