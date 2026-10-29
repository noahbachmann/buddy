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

| Item | Details |
|---|---|
| Motion controller | Raspberry Pi Pico 2 H (RP2350), firmware `pico-motion` flashed 2026-10-29, on the Pi's USB as `/dev/buddy-pico` (D17) |
| Servos | ⚠️ not noted yet (2.2 parts list: MG90S metal-gear micro servo, 180°) |
| Servo supply | ⚠️ not noted yet (2.2 parts list: 5 V / 5 A brick, 5.5 × 2.1 mm, 1000 µF on the rail) |

## Dev machines

| Item | Details |
|---|---|
| PC | Dev's home PC with an NVIDIA GPU: CLion, speech-to-text training (`docs/voice-training.md`) |
| Dev VM | Proxmox VM (x86_64, no GPU) used by Claude Code for edits, compile checks, eval runs against the Pi |
