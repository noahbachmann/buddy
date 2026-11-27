# Buddy – Hardware & Software Context

Current hardware and software used for the Buddy desk robot. Roadmap, architecture, decisions and progress live in `CLAUDE.md`.

---

## 3D printing & design

| Item | Details |
|---|---|
| Printer | Bambu Lab X2D + AMS: dual nozzle, enclosed. Build volume 256 × 256 × 260 mm with one nozzle, 235.5 × 256 × 256 mm with both |
| Filament | PETG for structure, load and anything near heat; PLA for low-load fits away from heat, support interfaces, jigs (rules: `CLAUDE.md` → Output formats, `docs/3d-printing.md`). No TPU |
| CAD | Onshape |

## Compute & power

| Item | Details |
|---|---|
| Board | Raspberry Pi 5, 8 GB |
| Cooling | Official Raspberry Pi 5 Active Cooler (aluminium heatsink + PWM fan on the 4-pin FAN header, push-pin mount) |
| Power | Official Raspberry Pi 27W USB-C power supply |
| Storage | 128 GB microSD |

## Audio

| Item | Details |
|---|---|
| Mic + speaker | Jabra Speak 510 MS (USB speakerphone, microphone and speaker in one device) |
| Music | Music box: **Google Nest Audio** "Main speaker", LAN 192.168.1.221 (fixed in the router). Plays from the Pi over **Bluetooth** (A2DP, SBC; D20): Soloist on the Pi → speakers connects it on demand. Also a Cast device (phones can cast to it directly). Music never plays through the Jabra |
| Other speakers | **Google Nest Mini**, Bluetooth alias "Bathroom speaker" (its Cast name: "Bathroom speaker haha"), LAN 192.168.1.222 (fixed in the router), −69 dBm at the Pi. Only one Nest connected at a time (the Pi's Bluetooth refuses a second A2DP link; a USB adapter might allow two ⚠️). Nest mics are not accessible |
| Bluetooth | Pi 5 on-board (BlueZ 5.82, PipeWire `libspa-0.2-bluetooth`); Pi's Wi-Fi on 5 GHz, so no 2.4 GHz contention |

## Smart home

| Item | Details |
|---|---|
| Hue Bridge | Philips Hue Bridge v2 (square, model BSB002), fixed LAN IP 192.168.1.219 (router). Buddy talks to it locally (D18) |
| Hue lights | 5 (`deploy/run.sh hue --cmd list`, 2026-10-30): colour "Bedroom Ceiling"; white "Kitchen Ceiling", "Living Room Corner"; colour "Hue color lamp 2" and "3" (in no room). Room "Home" (the three ceiling/corner lights), zones "Bedroom" and "Living Room", each with scenes |

## Card dispenser (Phase 3, `parts/dispenser`, #93)

All bought 2026-11-26 (parts per module: `parts/dispenser/README.md` → Parts).

| Item | Details |
|---|---|
| Controller | BTT SKR Pico V1.0: 4 × TMC2209 on board (3 used), endstop inputs, 12–24 V |
| Motors | 2 × NEMA 17 17HS4401 (lift, pusher), 1 × NEMA 17 pancake 17HS4023 (roller); cables 6p PH2.0 → 4p XH2.54 |
| Motion | 2 × TR8 lead screw 200 mm, pitch 8 (printed nuts); 2 × mechanical limit switch (lift home + spare; the pusher homes by stall detection) |
| Roller | 3 × NBR O-ring 80 × 3 mm (belts), 4 × 685ZZ bearing, Ø5 × 20 dowel pins (drum axles), M3 grub screw |
| Power | 12 V 4 A plug-in supply (5.5/2.1) + DC socket with screw terminals |
| Screws | M3 stainless assortment (340 pcs) |

## Motion controller (side project, `docs/motion-controller/`)

State 2026-10-30: one Pico with one servo (ch0) on the breadboard; Pico not on the Pi's USB yet.

| Item | Details |
|---|---|
| Motion controller | 2 × Raspberry Pi Pico 2 H (RP2350). One in use: firmware `pico-motion` flashed 2026-10-29, on the Pi's USB as `/dev/buddy-pico` (D17). The second is a spare |
| Servos | 2 × MG90S metal-gear micro servo, 180°. Servo 1 on ch0 (GP2) = the `demo` joint; servo 2 for ch1 (GP3) |
| Servo supply | Purecrea 5 V / 5 A plug-in supply (5.5 × 2.1 mm) + Purecrea DC socket with screw terminals, as in the `pico.md` parts list |
| Breadboard | Half size: 400 holes, 30 columns, with pre-cut solid copper jumper wires. `pico.md` step 3 is laid out for it (Pico in columns 1–20, two servos in 21–30). No room for a third servo |
| Wiring | Assorted jumper wires (Dupont) |
| Capacitors | Electrolytic 1000 µF, several (one on the servo rail) |
| Resistors | Assortment (330 Ω used: one per servo signal line) |
| Multimeter | XL830L, backlit (instead of the UNI-T UT33C+ in the parts list). Sockets: COM, VΩmA, 10A |

## Dev machines

| Item | Details |
|---|---|
| PC | Dev's home PC with an NVIDIA GPU: CLion, speech-to-text training (`docs/voice-training.md`) |
| Dev VM | Proxmox VM (x86_64, no GPU) used by Claude Code for edits, compile checks, eval runs against the Pi |
