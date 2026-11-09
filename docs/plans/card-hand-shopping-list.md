# Card dispenser – shopping list

Everything to buy for `parts/dispenser` (roller, lift, pusher). Screw counts are in the dispenser README → Parts.
**Ordered 2026-11-09**: every part the dispenser needs (Dev). Not arrived yet. Status: ordered · not needed ·
later (another step).

## Motors and motion

| Status | Part | Qty | Link | Note |
|---|---|---|---|---|
| ordered | Stepper NEMA 17 17HS4401, 42×42×40 | 2 | [Bastelgarage](https://www.bastelgarage.ch/components/stepper-motors/stepper-motors-1-124/42x42x40mm-stepper-motor-nema-17-17hs4401-1-7a-0-45nm) | Lift, pusher |
| ordered | Stepper NEMA 17 pancake 17HS4023, 42×42×23 | 1 | [Bastelgarage](https://www.bastelgarage.ch/components/stepper-motors/stepper-motors-1-124/42x42x23mm-stepper-motor-nema-17-pancake-17hs4023-0-7a-0-15nm) | Roller |
| ordered | Cable 6p PH2.0 → 4p XH2.54, 1 m | 3 | [Bastelgarage](https://www.bastelgarage.ch/6p-ph2-0-to-4p-xh2-54-stepper-motor-cable-1m-nema17) | Motor → SKR Pico. The motors' own cables end in a 4-pin plug that may not be XH |
| ordered | Flexible coupling 8 / 5 mm | 2 | [Bastelgarage](https://www.bastelgarage.ch/components/stepper-motors/stepper-motors-1-124/8mm-5mm-flexible-aluminium-coupling-with-dowel-screws) | |
| ordered | TR8 lead screw 200 mm, pitch 8 | 2 | [Bastelgarage](https://www.bastelgarage.ch/tr8-200mm-trapezoidal-threaded-spindle-pitch-8mm) | Used uncut: the pusher's sticks 75 mm out of the box's left side, the lift's 47 mm above the top. Not the M8 threaded rod (1.25 per turn, 6.4× slower) |
| not needed | ~~TR8 brass nut, pitch 8~~ | 0 | [Bastelgarage](https://www.bastelgarage.ch/tr8-trapezoidal-nut-brass-pitch-8mm) | Not needed: printed (`tr8-nut.scad`). Fallback if the printed nut wears: same pitch as the screw (not the 2 mm nut) |
| ordered | Mechanical limit switch with cable | 2 | [Bastelgarage](https://www.bastelgarage.ch/mechanical-limit-switch-with-cable) | Lift home + 1 spare (the pusher homes by stall detection). Check the plug fits the SKR Pico |
| not needed | KFL08 bearing with bracket | 2 | [Bastelgarage](https://www.bastelgarage.ch/mechanical-components/guides-spindles/kfl08-8mm-ball-bearing-with-horizontal-bracket) | Not used by the design |

## Roller

| Status | Part | Qty | Link | Note |
|---|---|---|---|---|
| ordered | NBR O-ring 80 × 3 mm, 70 Shore A | 3 | [Tameson (20 pcs)](https://tameson.com/products/s2657-x20-nbr-o-ring-80-x-3mm-od-86mm-70-shore-a-20-pieces) | The belts. 80 = inner diameter. ⚠️ Ships from the NL; no Swiss shop found |
| ordered | Ball bearing 5×11×5 (685ZZ) | 4 | [Bastelgarage](https://www.bastelgarage.ch/groove-ball-bearing-685zz-5x11x5mm) | Drums (#59). Buy 1–2 spares for the untested press fit |
| ordered | Dowel pin Ø5 × 20 mm, ISO 8734, RS PRO (20 pcs) | 4 | [Galaxus](https://www.galaxus.ch/en/s4/product/rs-pro-dowel-pins-5x20mm-20-pcs-wall-plugs-9815401) | Drum axles, one per bearing: nothing to cut (#57, #60) |
| ordered | M3 grub screw, 5 mm | 1 | [SFS (M3×5, 100 pcs)](https://www.sfs.ch/CH/en/dl/p/118668) or [RS (M3×6, 50 pcs)](https://ch.rs-online.com/web/p/gewindestifte/0137720) | Motor gear onto the 4023's shaft. Check the M3 assortment first. ⚠️ SFS may sell to businesses only; M3×6 stands 1 mm out of the hub |

## Electronics and power

| Status | Part | Qty | Link | Note |
|---|---|---|---|---|
| ordered | BTT SKR Pico V1.0 | 1 | [3DJake](https://www.3djake.ch/en-CH/bigtreetech/skr-pico) | 4 × TMC2209 on board (3 used), endstop inputs, 12–24 V |
| ordered | 12 V 4 A power supply, 5.5/2.1 plug | 1 | [Bastelgarage](https://www.bastelgarage.ch/components/power-supplies-electricity/plug-in-power-supply/12v-dc-4000ma-power-supply-ac-dc-adapter-5-5mm-2-1mm-plug) | |
| ordered | DC socket 5.5/2.1 with screw terminals | 1 | [Galaxus](https://www.galaxus.ch/en/s1/product/purecrea-dc-socket-female-barrel-jack-55mm-21mm-with-screw-terminals-cables-plugs-34239719) | Power supply → the board's power terminal |

## Screws

| Status | Part | Qty | Link | Note |
|---|---|---|---|---|
| ordered | M3 screw assortment, 340 pcs, stainless | 1 | Bastelgarage | Needs 7 × M3×6, 6 × M3×8, 15 × M3×12, 5 nuts, 2 washers: check the ×12 count |
| later | [ruthex M3 threaded inserts, M3×5.7, 100 pcs](https://www.3djake.ch/en-CH/ruthex/threaded-insert-m3-100-pieces) | 1 | 3DJake | Not for the dispenser (self-tapping pilot holes): head and body (2.5, 2.6) use them everywhere |
| later | [CNC Kitchen soldering tips + TS100 adapter](https://3djake.ch/en-CH/cnc-kitchen/melting-aids-ts100-adapter) | 1 | 3DJake | Insert tips M2–M8 for a Pinecil/TS100 (the iron itself: not at 3DJake) |
