// Hex driver for M3 socket head screws (ISO 4762: 2.5 mm hex, 2.89 mm corner to corner). All dimensions in mm.
// Prints standing, tip up: +Z = build direction.

$fn = 96;

// --- tip: corner-to-corner sizes, one driver each (the printer rounds corners, so the fit comes from trying) ---
tip_c2c     = [3.0, 2.9, 2.8];
tip_len     = 10;      // exposed hex; short = stiffer in torsion, still reaches a 3 mm deep head
tip_chamfer = 0.3;     // eases the tip into the socket

// --- shank: 45° cone from the handle into the tip, no step where the tip would snap ---
shank_d     = 8;

// --- handle ---
handle_d    = 20;
handle_len  = 55;
flutes      = 6;       // grip grooves around the handle
flute_d     = 6;
flute_depth = 1.2;
top_round   = 2;       // rounded top edge of the handle
bed_chamfer = 0.4;     // against elephant foot (docs/3d-printing.md → Design rules)
label_depth = 0.6;     // size engraved into the bed face
label_size  = 6;
