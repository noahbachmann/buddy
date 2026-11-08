# Card dispenser – shopping list

Everything to buy for `parts/dispenser` (roller, lift, pusher). Screw counts are in the dispenser README → Parts.
✓ = in the Bastelgarage cart.

## Motors and motion

| ✓ | Part | Qty | Link | Note |
|---|---|---|---|---|
| [x] | Stepper NEMA 17 17HS4401, 42×42×40 | 2 | [Bastelgarage](https://www.bastelgarage.ch/components/stepper-motors/stepper-motors-1-124/42x42x40mm-stepper-motor-nema-17-17hs4401-1-7a-0-45nm) | Lift, pusher |
| [x] | Stepper NEMA 17 pancake 17HS4023, 42×42×23 | 1 | [Bastelgarage](https://www.bastelgarage.ch/components/stepper-motors/stepper-motors-1-124/42x42x23mm-stepper-motor-nema-17-pancake-17hs4023-0-7a-0-15nm) | Roller |
| [ ] | Cable 6p PH2.0 → 4p XH2.54, 1 m | 3 | [Bastelgarage](https://www.bastelgarage.ch/6p-ph2-0-to-4p-xh2-54-stepper-motor-cable-1m-nema17) | Motor → SKR Pico. The motors' own cables end in a 4-pin plug that may not be XH |
| [x] | Flexible coupling 8 / 5 mm | 2 | [Bastelgarage](https://www.bastelgarage.ch/components/stepper-motors/stepper-motors-1-124/8mm-5mm-flexible-aluminium-coupling-with-dowel-screws) | |
| [ ] | TR8 lead screw 200 mm, pitch 8 | 2 | [Bastelgarage](https://www.bastelgarage.ch/tr8-200mm-trapezoidal-threaded-spindle-pitch-8mm) | Cut: pusher 130 mm, lift 175 mm |
| [x] | TR8 brass nut, pitch 8 | 2 | [Bastelgarage](https://www.bastelgarage.ch/tr8-trapezoidal-nut-brass-pitch-8mm) | Same pitch as the screw (not the 2 mm nut) |
| [x] | Mechanical limit switch with cable | 4 | [Bastelgarage](https://www.bastelgarage.ch/mechanical-limit-switch-with-cable) | Lift home + spares (the pusher can home by stall detection). Check the plug fits the SKR Pico |
| [x] | KFL08 bearing with bracket | 2 | [Bastelgarage](https://www.bastelgarage.ch/mechanical-components/guides-spindles/kfl08-8mm-ball-bearing-with-horizontal-bracket) | Not used by the design: can go |

## Roller

| ✓ | Part | Qty | Link | Note |
|---|---|---|---|---|
| [ ] | NBR O-ring 80 × 3 mm, 70 Shore A | 3 | [Tameson (20 pcs)](https://tameson.com/products/s2657-x20-nbr-o-ring-80-x-3mm-od-86mm-70-shore-a-20-pieces) | The belts. 80 = inner diameter. ⚠️ Ships from the NL; no Swiss shop found |
| [ ] | Ball bearing 5×10×4 (MR105ZZ) | 4 | [Digitec (10 pcs)](https://www.digitec.ch/en/s1/product/oem-ball-bearing-5x10x4mm-rc-model-accessories-5815058) | Drums; the spares cover the untested press fit |
| [ ] | Steel shaft Ø5 mm h6, 0.4 m | 1 | [RS (Ewellix)](https://ch.rs-online.com/web/p/linearwellen/4232403) | 2 drum axles, 119.9 mm. Hardened: cut with a Dremel cut-off disc, not a hacksaw |
| [ ] | M3 grub screw, 5 mm | 1 | [Galaxus (50 pcs)](https://www.galaxus.ch/en/s4/product/toolcraft-grub-screw-m3-5-mm-stainless-steel-50-screws-per-piece-screws-8515989) | Motor gear onto the 4023's shaft |

## Electronics and power

| ✓ | Part | Qty | Link | Note |
|---|---|---|---|---|
| [ ] | BTT SKR Pico V1.0 | 1 | [3DJake](https://www.3djake.ch/en-CH/bigtreetech/skr-pico) | 4 × TMC2209 on board (3 used), endstop inputs, 12–24 V |
| [x] | 12 V 4 A power supply, 5.5/2.1 plug | 1 | [Bastelgarage](https://www.bastelgarage.ch/components/power-supplies-electricity/plug-in-power-supply/12v-dc-4000ma-power-supply-ac-dc-adapter-5-5mm-2-1mm-plug) | |
| [ ] | DC socket 5.5/2.1 with screw terminals | 1 | [Galaxus](https://www.galaxus.ch/en/s1/product/purecrea-dc-socket-female-barrel-jack-55mm-21mm-with-screw-terminals-cables-plugs-34239719) | Power supply → the board's power terminal |

## Screws

| ✓ | Part | Qty | Link | Note |
|---|---|---|---|---|
| [x] | M3 screw assortment, 340 pcs, stainless | 1 | Bastelgarage | Needs 5 × M3×6, 8 × M3×8, 15 × M3×12, 5 nuts, 2 washers: check the ×12 count |
| [x] | M3 heat-set inserts, 3 mm and 5 mm, 100 pcs each | 2 | Bastelgarage | Spare: the design uses self-tapping pilot holes |
