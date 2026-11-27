# Card dispenser: status

Where the dispenser (`parts/dispenser`, Buddy's hand piles H and B for Riftbound 5.7+) stands right now: what's
printed, what waits for a print, what changed lately. How it works and why: `parts/dispenser/README.md`. Fits and the
print log: `docs/3d-printing.md`. Update this file with every dispenser change (newest first under "Changes").

## Parts

All bought parts are here (2026-11-26; list archived in `docs/archive/card-hand-shopping-list.md`). Needed now:
README → Parts. The 4 dowel pins (drum axles) come from the 20-pin pack.

## Plates

Status: ✅ printed and fine · 🔁 printed, replaced by a newer plate · ⏳ to print · ❓ not recorded yet (Dev: fill in).

| Plate | Parts | Status |
|---|---|---|
| `0-fit-test` | fit test, test pins | ✅ 2026-11-09 (log #3) |
| `1-petg-fine` | drums, motor gear, left gate, nuts, couplings, border pins | 🔁 gear and nuts too tight (log #5) → `1c` |
| thread test (was `1b`) | TR8 nut and D-bore rings | ✅ 2026-11-17 (log #7); the 1b lift nut failed (log #6) |
| `1c-reprint` | motor gear, pusher nut, lift nut at the thread-test values | ❓ |
| `1d-plug-couplings` | 2 plug couplings (replace the clamp couplings) | ❓ |
| `2-petg` | roller frame (one piece), borders H and B | 🔁 pin holes off (log #8), borders → `2c` |
| `0c-pilot-test` | M3 pilots Ø2.6–3.0 | ✅ 2026-11-25 (log #9): Ø2.7 |
| `2b-roller-reprint` | roller frame + back plate (split), Ø5.05 pin holes, Ø2.7 pilots | ⏳ |
| `2c-borders` | borders H and B with the tie slots, 3 more border pins | 🔁 2026-11-26: B's 2 pin holes from below very slightly loose (log #10); tie slots hold nothing against a push to the front → `2d` |
| `2d-borders` | border H with blind holes for 2 divider pins, border B (no slots, floor holes −0.025), 2 more border pins | ⏳ |
| `3-pla` | pusher, lift | ❓ |
| `stl/case` or `stl/case-spares` | case | ❓ Changed 2026-11-27: the divider's 2 holes at (y, z) = (−148, 73), (−148, 87) are Ø3.4 with a Ø6.4 × 1.4 counterbore on pile B's side (were Ø5.05 tie holes, 2026-11-26, which also added 3 Ø3.4 holes in the right wall). A case printed before needs a reprint ⚠️ |

## Next

1. Print `2b` and `2d-borders` (PETG, plate 2's settings, no brim) and the case.
2. Border H in, its 2 pins through the left wall and 2 through the divider from pile B's side; then border B and its 5 pins (README → Assembly 8, 9).
3. Roller: back plate in from the left, 4023 + gear + back pins, drums and O-rings, frame from the front, 4 × M3×12
   from below. Then the gap (README → Tuning the gap).
4. Open in the README: **feed stops short** (software test first: fast feed) and the untested assumptions (card
   thickness, pile height, bearing press fit).

## Changes

Newest first. Details in the commit bodies (`git log -- parts/dispenser`).

| Date | Change | Print |
|---|---|---|
| 2026-11-27 | Ties dropped (slots open to the front: a push to the front slid the borders off): border H held at the divider by 2 border pins from pile B's side, heads sunk in counterbores, B in after H covers them; border B nothing at the divider | `2d`, case |
| 2026-11-26 | Border B's pin holes from below −0.025 per side (were 0: the hanging pins fell out of the 2c print) | `stl/border-b` |
| 2026-11-26 | Front borders held at both ends: 2 Ø5×20 dowel pins through the divider, borders slide on from the front; border B + 3 printed pins through the right wall | `2c` |
| 2026-11-25 | M3 pilots Ø2.5 → Ø2.7 (pilot test) in the roller frame's and back plate's feet | `0c`, then `2b` |
| 2026-11-24 | Roller split: frame (front plate + bed + ribs) and back plate on its own; rib tongues into the back plate, now printed from the plate up | `2b` |
| 2026-11-24 | Steel pin holes Ø5.05 in both roller plates (were 4.9 / 5.2 as printed) | `2b` |
| 2026-11-19 | Plug coupling: pressed on the shaft, screw threaded in, no hardware | `1d` |
| 2026-11-17 | Nut flank 0.10, bore gap 0.125 (thread test); rigid clamp coupling replaced the flexible ones | `1c` |
