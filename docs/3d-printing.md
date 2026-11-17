# 3D printing

My notes for the X2D. ⚠️ = not tested by me yet.

## Every print

1. Filament dry?
2. Plate clean, no fingers on it.
3. Right plate selected in Bambu Studio.
4. Only one filament on the plate (unless I want PLA supports).
5. Watch the first layer.
6. Let it cool, flex the plate, log the print.

## Materials

- **PETG**: structure, real load (brackets, servo mounts, arm links), anything near heat (motors, Pi, drivers).
- **PLA**: low-load functional parts away from heat (stiffer and more accurate than PETG: fits, sliding edges, e.g.
  the dispenser's lift and pusher), support interface under PETG, mock-ups, jigs. Soft at ~55 °C ⚠️. Self-tapping
  screws: by hand, stop when snug (cracks more easily than PETG).

| | PETG | PLA |
|---|---|---|
| Profile | Bambu PETG preset | Bambu PLA preset |
| Dry | 65 °C, several hours ⚠️ | only if old |
| Plate | Textured PEI. Smooth PEI only with glue | any |

## Settings: what to pick

Start from Bambu's preset (printer, 0.4 nozzle, filament). Then pick the row that fits the part:

| Part | Layer | Walls | Infill | Example |
|---|---|---|---|---|
| Mock-up, test fit | 0.28 | 2 | 10 % | Cardboard replacement, size check |
| Normal | 0.20 | 3 | 15 % grid | Trays, covers, cable clips |
| Strong (takes load) | 0.20 | 5–6 | 40 % gyroid | Servo mounts, brackets, arm links |
| Precise (fits) | 0.12–0.16 | 3–4 | 15 % | Lids, snap fits, gears |
| Looks (visible) | 0.12–0.16 | 3 | 15 % | Head shell, body outside |

Strength comes mostly from **walls**, not infill. More walls first, more infill second.

### Each setting, when to change it

| Setting | Default | Change when |
|---|---|---|
| Layer height | 0.20 | Lower (0.12–0.16): curves, fits, fine detail. Higher (0.28): fast drafts. Lower = slower |
| Wall loops | 2 | Thin walls: wall thickness ÷ 0.4 (1.6 mm → 4). Strong parts: 5–6 |
| Top/bottom layers | ~4–5 | Thin plates: make them solid (1.6 mm ÷ 0.2 → 8) |
| Infill % | 15 % | Load: 30–40 %. Tiny parts under load: 100 % |
| Infill pattern | grid | Load from many directions: gyroid |
| Supports | off | Overhang steeper than 45° that I can't avoid by rotating: tree supports. PETG part: PLA interface |
| Brim | off | Tall and thin, or tiny footprint (tips over or lifts) |
| Ironing | off | Flat top that must look smooth. Slow |
| Seam | aligned | Visible parts: "back", so the seam line is out of sight |
| X-Y contour compensation | 0 | Part too big to fit into another: −0.1 |
| X-Y hole compensation | 0 | Holes too small: +0.1 |

Change things for one part only: right-click it → Add settings. Keeps the preset clean.

## Design rules

- Walls in steps of 0.4 mm.
- Walls, plates and tabs **3 mm max** (Dev). Thicker only where a part takes real load, and say why in the model. Stiffness
  comes from shape (U sections, ribs, side walls), not from solid blocks.
- Overhangs max 45° ⚠️. Chamfer bottom edges, don't round them.
- Holes ≥ 1.5 mm ⚠️.
- **Every hole takes its gap from the Fits table**, never a nominal or hand-picked size: a Ø5.15 bore for a 5 mm
  shaft (picked before the fit test) didn't go on.
- **Threads:** the flanks carry, not the crests. A gap added to the diameter opens each flank only by
  gap × sin(half angle): TR (15°) ≈ ¼, metric (30°) ½. Size thread gaps per flank (BOSL2's `$slop` is radial: `$slop`
  = flank gap / sin(half angle) / 2). Internal threads print standing (axis Z): the downward flanks sag, so they come
  out tighter still.
- **Shaft bores** (motor shafts, screws): slip fit, plus a 0.4 mm chamfer at the bed face against elephant foot.
- Clamp a shaft with a slotted hub and a bolt across the slot, not a grub screw in a bore with play: the clamp
  centres the shaft along its length, a grub screw tilts it (~1° in a 12 mm bore at 0.1 per side).
- Strongest along the layers: orient so it doesn't split between them.
- Largest flat face down.

## Fits

Gap per side between flat walls (lid rim in box). Round holes print smaller: their fits come from the fit-test plate.

| Fit | Material | Gap | Result |
|---|---|---|---|
| Tray lid in box | PETG | 0.15 mm | Slightly too loose |
| Tray lid in box | PETG | 0.10 mm | Still too loose |
| Tray lid in box | PLA | 0.07 mm | Press fit, good. 0.065 at most would be a touch tighter, never looser than 0.07 |
| Slides freely | | ? | |
| **Press fit** | PLA | **0.07 mm** | From the tray lid |

Round holes, PETG at 0.20 mm (fit test, log #3), gap per side:

| Hole | Press | Slip |
|---|---|---|
| Vertical (axis Z) | +0.05 | +0.10 |
| Horizontal teardrop | 0 | +0.05 |
| TR8 nut, per flank (standing) | ? | 0.15 ⚠️ thread test (log #5) |

## Problems

| Problem | Fix |
|---|---|
| Doesn't stick | Wash plate, check plate setting |
| Spaghetti mid-print | A part came loose: small footprint under a wide or tall top → brim (outer). On a one-object plate set it by hand: "auto" judges the whole plate |
| Strings | Dry the filament |
| PETG ripped the plate | Glue on smooth PEI |
| Too tight | Contour compensation, then fix the CAD |
| Thread too tight | Gap was set radially: size it per flank (Design rules) |

## Log

| # | Date | Part | Changes | Result | Notes |
|---|---|---|---|---|---|
| 1 | 2026-11 | Desiccant tray | walls 4, layers 8 | Lid slightly too loose (0.15 per side) | Lid rim widened to 0.10, then 0.07 per side |
| 2 | 2026-11 | Desiccant trays (PLA) | lid rim 0.07 per side | Press fit, holds | Keep 0.07 for PLA press fits |
| 3 | 2026-11-09 | Dispenser fit test (PETG) | Ø3 pin in holes −0.05…+0.20 per side | Vertical: press +0.05, slip +0.10. Horizontal teardrop: press 0, slip +0.05 | `fit_*` in `dispenser/params.scad` |
| 4 | 2026-11 | Dispenser borders H, B (PETG) | Stale plate: H vertical +0 per side, B horizontal +0.05 | H too tight, B pins slide out | Fit test confirmed in 7 mm deep holes: H +0.05, B 0 |
| 5 | 2026-11-16 | Dispenser motor gear, TR8 nuts (PETG, 0.12) | Gear bore Ø5.15 (+0.075/side); nuts 0.2 radial (= 0.05 per flank) | Neither went on | Bore → slip +0.10 with bed chamfer; nuts → 0.15 per flank; thread test rings 0.10–0.25 per flank, bores 0.10–0.20 (`plates/1b-reprint`) ⚠️ results open |
| 6 | 2026-11-17 | Dispenser 1b reprint (PETG, 0.12), no brim | – | Lift nut came loose: spaghetti | Stands on ~30 mm² (16 in layer 1) under a Ø22 flange 15 mm up: brim (outer, 5 mm), reprint it alone |
