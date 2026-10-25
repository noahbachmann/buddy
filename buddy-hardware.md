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

## Dev machines

| Item | Details |
|---|---|
| PC | Dev's home PC with an NVIDIA GPU: CLion, speech-to-text training (`docs/voice-training.md`) |
| Dev VM | Proxmox VM (x86_64, no GPU) used by Claude Code for edits, compile checks, eval runs against the Pi |
