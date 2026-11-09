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
- Overhangs max 45° ⚠️. Chamfer bottom edges, don't round them.
- Holes ≥ 1.5 mm ⚠️.
- Strongest along the layers: orient so it doesn't split between them.
- Largest flat face down.

## Fits

Gap per side, PETG.

| Fit | Gap | Result |
|---|---|---|
| Tray lid in box | 0.15 mm | Slightly too loose |
| Tray lid in box | 0.10 mm | Still too loose |
| Tray lid in box | 0.07 mm | ? |
| Slides freely | ? | |
| Press fit | ? | |

## Problems

| Problem | Fix |
|---|---|
| Doesn't stick | Wash plate, check plate setting |
| Strings | Dry the filament |
| PETG ripped the plate | Glue on smooth PEI |
| Too tight | Contour compensation, then fix the CAD |

## Log

| # | Date | Part | Changes | Result | Notes |
|---|---|---|---|---|---|
| 1 | 2026-11 | Desiccant tray | walls 4, layers 8 | Lid slightly too loose (0.15 per side) | Lid rim widened to 0.10, then 0.07 per side |
