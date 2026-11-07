# Card Hand – Shopping List

Parts for the two-column card sorter (2 lead-screw lifts + 1 feed roller). Prices from Bastelgarage, checked 2026-11-01.

> ⚠️ **Before ordering:** decide the controller board (it decides which motor/endstop cables you need) and confirm the hand is the two-column sorter, not a gripping hand.

Tick a box by changing `[ ]` to `[x]`.

## Lead-screw lifts (2 sets)

> ⚠️ The pusher (`parts/dispenser`) has no coupling, and Dev's lift model has none either: both need the 17HS4401
> **with integrated TR8x8 lead screw** (sold as "17HS4401S-T8x8", usually 300 mm and up, cut to length). That
> replaces motor + coupling + screw below. Not found at Bastelgarage (searched 2026-11-07).

| ✓ | Part | Qty | Each (CHF) | Total (CHF) | Link | Notes | 
|---|---|---|---|---|---|---|
| [ ] | NEMA17 17HS4401, 42×42×40 mm | 2 | 14.90 | 29.80 | [Bastelgarage](https://www.bastelgarage.ch/components/stepper-motors/stepper-motors-1-124/42x42x40mm-stepper-motor-nema-17-17hs4401-1-7a-0-45nm) | Standard printer-size motor | 
| [ ] | Cable 6p PH2.0 → 4p XH2.54, 1 m | 2 | 2.90 | 5.80 | [Bastelgarage](https://www.bastelgarage.ch/components/stepper-motors/stepper-motors-1-124/6p-ph2-0-to-4p-xh2-54-stepper-motor-cable-1m-nema17) | connects 17hs4401 to skr pico. |
| [ ] | Flexible coupling 8 mm / 5 mm | 2 | 3.90 | 7.80 | [Bastelgarage](https://www.bastelgarage.ch/components/stepper-motors/stepper-motors-1-124/8mm-5mm-flexible-aluminium-coupling-with-dowel-screws) | Motor shaft (5 mm) → lead screw (8 mm) |
| [ ] | TR8 lead screw 200 mm, pitch 8 mm | 2 | 9.90 | 19.80 | [Bastelgarage](https://www.bastelgarage.ch/mechanical-components/guides-spindles/tr8-200mm-trapezoidal-threaded-spindle-pitch-8mm) | Shortest length available |
| [ ] | TR8 brass nut, pitch 8 mm | 2 | 4.90 | 9.80 | [Bastelgarage](https://www.bastelgarage.ch/mechanical-components/guides-spindles/tr8-trapezoidal-nut-brass-pitch-8mm) | ⚠️ Must be the **8 mm** nut to match the screw, not the 2 mm one |
| [ ] | KFL08 bearing (optional) | 2 | 3.90 | 7.80 | [Bastelgarage](https://www.bastelgarage.ch/mechanical-components/guides-spindles/kfl08-8mm-ball-bearing-with-horizontal-bracket) | Supports the top end of the screw |
| [ ] | Mechanical limit switch with cable | 4 | 3.90 | 15.60 | [Bastelgarage](https://www.bastelgarage.ch/mechanical-limit-switch-with-cable) | 2 bottom homing + 2 top-of-stack. 3-pin Dupont cable: fits a breadboard, needs an adapter for a printer board. ⚠️ Stock status unclear on the page |
| [ ] | Controller board | 1 | | | [3djake](https://www.3djake.ch/en-CH/bigtreetech/skr-pico?sai=11949) | Undecided: SKR Pico or plain Pico + driver modules |

## Roller motor

| ✓ | Part | Qty | Each (CHF) | Total (CHF) | Link | Notes |
|---|---|---|---|---|---|---|
| [ ] | NEMA17 pancake 17HS4023, 42×42×23 mm | 1 | 14.90 | 14.90 | [Bastelgarage](https://www.bastelgarage.ch/components/stepper-motors/stepper-motors-1-124/42x42x23mm-stepper-motor-nema-17-pancake-17hs4023-0-7a-0-15nm) | 0.15 Nm, stronger than the 0.09 Nm pancake at the same price |


## Power

| ✓ | Part | Qty | Each (CHF) | Total (CHF) | Link | Notes |
|---|---|---|---|---|---|---|
| [ ] | 12 V DC 4 A power brick, 5.5/2.1 mm plug | 1 | 16.90 | 16.90 | [Bastelgarage](https://www.bastelgarage.ch/components/power-supplies-electricity/plug-in-power-supply/12v-dc-4000ma-power-supply-ac-dc-adapter-5-5mm-2-1mm-plug) | Closed brick, no exposed mains terminals |
| [ ] | DC jack → screw terminal adapter, 5.5/2.1 mm | 1 | | | [Bastelgarage](https://www.bastelgarage.ch/dc-socket-female-barrel-jack-5-5mm-2-1mm-with-screw-terminals?search=DC%20Socket%20Female%20Barrel%20Jack%205.5mm%20%2F%202.1mm%20with%20Screw%20Terminals) | |

## general stuff

| ✓ | Part | Qty | Each (CHF) | Total (CHF) | Link | Notes |
|---|---|---|---|---|---|---|
| [ ] | M3 screws, nuts, grub screws, heat inserts | 1 set | | | | all in galaxus list |
| [ ] | Rubber O-rings (~20 mm ID, 3 mm cord) or silicone tube | 1 pack | | | | Roller grip surface |
| [ ] | Digital caliper | 1 | | | | |

## can be 3d printed
| ✓ | Part | Qty | Each (CHF) | Total (CHF) | Link | Notes |
|---|---|---|---|---|---|---|
| [ ] | 8 mm smooth rod | ~600 mm | | | | 4 × ~150 mm, 2 per lift |
| [ ] | LM8UU linear bearing | 4 | 3.90 | 15.60 | [Bastelgarage](https://www.bastelgarage.ch/mechanical-components/guides-spindles/lm8uu-linear-bearing-o8mm) | Slides on the rods |
