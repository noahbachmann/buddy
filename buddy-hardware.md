# Buddy – Hardware & Software Context

Current hardware and software used for the Buddy desk robot. Roadmap, architecture, decisions and progress live in `CLAUDE.md`.

---

## 3D printing & design

| Item | Details |
|---|---|
| Printer | Bambu Lab P2S |
| Filament | PLA (prototypes), PETG HF (functional / heat-exposed parts) |
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
| Music | Music box, a Spotify Connect speaker named "Main speaker" in the Spotify app (model ⚠️ not noted yet). Buddy controls it via the Web API; music never plays through the Jabra |

## Smart home

| Item | Details |
|---|---|
| Hue Bridge | Philips Hue Bridge v2 (square, model BSB002), LAN 192.168.1.219 ⚠️ give it a fixed IP in the router. Buddy talks to it locally (D18) |
| Hue lights | 5 (`deploy/run.sh hue --cmd list`, 2026-10-30): colour "Bedroom Ceiling"; white "Kitchen Ceiling", "Living Room Corner"; colour "Hue color lamp 2" and "3" (in no room). Room "Home" (the three ceiling/corner lights), zones "Bedroom" and "Living Room", each with scenes |

## Motion (Phase 2, guide 2.2)

State 2026-10-30: one Pico with one servo (ch0) on the breadboard; Pico not on the Pi's USB yet.

| Item | Details |
|---|---|
| Motion controller | 2 × Raspberry Pi Pico 2 H (RP2350). One in use: firmware `pico-motion` flashed 2026-10-29, on the Pi's USB as `/dev/buddy-pico` (D17). The second is a spare |
| Servos | 2 × MG90 micro servo ⚠️ check the label: the 2.2 parts list asks for **MG90S** (metal gear, 180°); a plain "MG90" may be a different, weaker model. Servo 1 on ch0 (GP2) = head pan (left-right); runs as the `demo` joint until 2.5 renames it. Servo 2: head tilt (ch1, GP3) |
| Servo supply | 5 V plug-in supply + female DC socket (5.5 × 2.1 mm, screw terminals) ⚠️ model and rating not noted (2.2 parts list: 5 V / 5 A). Check polarity (centre +) with the multimeter before connecting |
| Breadboard | Small, 30 rows (half size, ~400 holes), with pre-cut solid copper jumper wires. ⚠️ The 2.2 wiring (step 3) is laid out for an 830-hole board (columns up to 36): the Pico 2 H takes 20 of the 30 rows, so servo headers and capacitor go in the remaining 10 |
| Wiring | Assorted jumper wires (Dupont) |
| Capacitors | Assortment ⚠️ values not noted: the servo rail needs a 1000 µF electrolytic (≥ 10 V, polarised) |
| Resistors | Assortment (330 Ω used: one per servo signal line) |
| Multimeter | XL830L, backlit (instead of the UNI-T UT33C+ in the parts list). Sockets: COM, VΩmA, 10A |

## Dev machines

| Item | Details |
|---|---|
| PC | Dev's home PC with an NVIDIA GPU: CLion, speech-to-text training (`docs/voice-training.md`) |
| Dev VM | Proxmox VM (x86_64, no GPU) used by Claude Code for edits, compile checks, eval runs against the Pi |
