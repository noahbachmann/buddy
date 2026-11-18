// L-shaped hex key for M3 socket head screws (ISO 4762: 2.5 mm hex, 2.89 mm corner to corner). All dimensions in mm.
// Prints lying flat: +Z = build direction, bar and hex flats on the bed. Long arm along +Y, short arm along +X.

$fn = 96;

// --- tip: corner-to-corner sizes, one key each (the printer rounds corners, so the fit comes from trying) ---
tip_c2c     = [2.6, 2.3];  // flat print: 2.8 still too big (Dev, 2026-11-18)
tip_len     = 10;      // exposed hex at both ends; short = stiffer in torsion, still reaches a 3 mm deep head
tip_chamfer = 0.3;     // eases the tip into the socket

// --- bar: the L, flat on the bed ---
bar_w       = 8;       // in the bed plane
bar_h       = 5;       // build height
top_round   = 1.5;     // rounded top edges (convex, faces up: no overhang)
bed_chamfer = 0.4;     // against elephant foot (docs/3d-printing.md → Design rules)
bend_r      = 6;       // bend radius on the bar's centre line
long_len    = 60;      // straight bar after the bend, without transition and tip
short_len   = 12;

// --- size engraved on the long arm's top ---
label_depth = 0.6;
label_size  = 3.5;
