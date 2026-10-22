# 1.4 – Audio setup

> Written 2026-10-22, not yet tested on this exact kit. Items marked ⚠️ are assumptions.

## Goal

Record from the Jabra's microphone and play through its speaker on the headless Pi, through PipeWire, as user `dev`. It must work right after boot without anyone logging in, and record in the format the speech pipeline needs later (16 kHz, mono, 16-bit).

## Prerequisites

- Step 1.3 done: `ssh buddy` works, system updated.
- Jabra plugged into a black USB 2 port, not muted (its mute LED is not red).

## Parts / tools

| Tool | Package | Role |
|---|---|---|
| PipeWire | `pipewire` | Audio server: lets several programs use the Jabra at the same time |
| WirePlumber | `wireplumber` | Session manager: finds devices, picks defaults, remembers them |
| pipewire-alsa | `pipewire-alsa` | Routes programs that use the ALSA API (e.g. `arecord`, later maybe our C++ code) through PipeWire |
| pipewire-pulse | `pipewire-pulse` | PulseAudio compatibility (librespot can use it in 1.9) |
| `wpctl`, `pw-record`, `pw-play` | `wireplumber`, `pipewire-bin` | Inspect, record, play |
| `arecord`, `aplay`, `speaker-test` | `alsa-utils` | Low-level checks |

## Background: why PipeWire, and why "lingering"

- **Without an audio server**, a program opens the Jabra directly and has it to itself. The next program that tries gets "Device or resource busy". Buddy needs several at once: ears records while voice and Spotify play. PipeWire sits in between and mixes.
- **PipeWire runs per user**, as part of that user's login session, not as a system-wide service. On a headless Pi, nobody logs in at boot, so by default there's no session and no audio. **Lingering** tells systemd to start `dev`'s session at boot anyway. Step 1.5's setup script does the same thing; doing it twice is harmless.

## Steps

### 1. Does the kernel see the Jabra?

```bash
cat /proc/asound/cards
```

Expected, among possibly HDMI entries ⚠️ exact name may differ:

```
 2 [USB            ]: USB-Audio - Jabra SPEAK 510 USB
                      GN Netcom A/S Jabra SPEAK 510 USB at usb-xhci-hcd.0-1, full speed
```

If the Jabra isn't listed, stop here and see Troubleshooting.

### 2. Install PipeWire (if missing)

⚠️ Raspberry Pi OS Lite may or may not ship PipeWire. This checks and installs whatever is missing:

```bash
sudo apt install -y pipewire pipewire-alsa pipewire-pulse wireplumber alsa-utils
```

Already installed packages are simply skipped.

### 3. Enable lingering and start the user audio services

```bash
sudo loginctl enable-linger "$USER"
systemctl --user enable --now pipewire.socket pipewire-pulse.socket wireplumber.service
systemctl --user is-active pipewire wireplumber
```

Expected: `active` twice. (`pipewire` may show `active` only after something used it; run `wpctl status` once and check again.)

### 4. Check PipeWire sees the Jabra

```bash
wpctl status
```

Look at the `Audio` section. Expected ⚠️ names and numbers vary:

```
Audio
 ├─ Devices:
 │      45. Jabra SPEAK 510 USB                [alsa]
 │      46. Built-in Audio                      [alsa]
 │      47. Built-in Audio                      [alsa]
 ├─ Sinks:
 │  *   48. Jabra SPEAK 510 USB Analog Stereo  [vol: 0.40]
 ├─ Sources:
 │  *   49. Jabra SPEAK 510 USB Mono           [vol: 1.00]
```

- **Sinks** = outputs (speaker), **Sources** = inputs (microphone).
- The `*` marks the default. Both defaults must be the Jabra.

If a default points elsewhere (e.g. HDMI), set it with the number from **your** output:

```bash
wpctl set-default 48   # Jabra sink
wpctl set-default 49   # Jabra source
```

WirePlumber remembers the choice across reboots ⚠️ (stored per device name, not per number).

### 5. Set sensible volumes

```bash
wpctl set-volume @DEFAULT_AUDIO_SINK@ 0.6
wpctl set-volume @DEFAULT_AUDIO_SOURCE@ 1.0
wpctl set-mute   @DEFAULT_AUDIO_SOURCE@ 0
wpctl get-volume @DEFAULT_AUDIO_SINK@     # Volume: 0.60
wpctl get-volume @DEFAULT_AUDIO_SOURCE@   # Volume: 1.00
```

The Jabra's own +/− buttons change its hardware volume on top of this.

### 6. Playback test

```bash
speaker-test -c 2 -t wav -l 1
```

Expected: the Jabra says "Front left", "Front right". (`speaker-test` uses the ALSA API, so this also proves the ALSA → PipeWire route works.)

### 7. Recording test in the speech format

Wake word, voice detection and whisper.cpp all want **16 kHz, mono, 16-bit**. Record exactly that:

```bash
pw-record --rate 16000 --channels 1 --format s16 ~/test-near.wav
```

Speak a sentence from about 1 m away, then press `Ctrl+C`. Play it back:

```bash
pw-play ~/test-near.wav
```

Repeat from 2–3 m (step 1.6 must trigger from there):

```bash
pw-record --rate 16000 --channels 1 --format s16 ~/test-far.wav
pw-play ~/test-far.wav
```

Same test through the ALSA API (the route our C++ code may use later):

```bash
arecord -D default -f S16_LE -r 16000 -c 1 -d 5 ~/test-alsa.wav
aplay ~/test-alsa.wav
```

Optional, to look at the waveform on the PC (e.g. in Audacity):

```bash
scp buddy:test-far.wav .
```

Listen for: your voice clearly audible, no crackling, fan hum low compared to your voice.

### 8. Check the Jabra's own echo cancellation (prep for 1.11)

The Jabra is built for phone calls, so it may already remove its own speaker sound from the microphone. Find out now:

```bash
speaker-test -c 2 -t pink -l 0 > /dev/null &      # pink noise through the speaker
pw-record --rate 16000 --channels 1 --format s16 ~/test-echo.wav
# talk normally for ~5 s, then Ctrl+C
kill %1
pw-play ~/test-echo.wav
```

Note the result for 1.11:

| What you hear | Meaning |
|---|---|
| Your voice clear, noise quiet or gone | Jabra cancels its own playback. 1.11 may only need ducking. |
| Noise as loud as or louder than your voice | No useful hardware echo cancellation. 1.11 needs PipeWire echo-cancel. |

### 9. Reboot test

```bash
sudo reboot
```

After ~30 s, without doing anything else:

```bash
ssh buddy
wpctl status          # Jabra still default sink and source
speaker-test -c 2 -t wav -l 1
```

Clean up test files when done: `rm ~/test-*.wav`.

## Verify

- [ ] Jabra in `/proc/asound/cards`.
- [ ] `wpctl status`: Jabra is the default sink **and** source (`*`).
- [ ] `speaker-test` plays through the Jabra.
- [ ] 16 kHz mono recording plays back clearly, also from 2–3 m.
- [ ] Echo test result noted.
- [ ] Still works after reboot without logging in first.

**Done when:** a test recording plays back clearly. ✅

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Jabra missing in `/proc/asound/cards` | USB connection | Other black USB port; check `lsusb`; `dmesg | tail -20` right after plugging in |
| `dmesg` shows `cannot set freq 48000 to ep 0x3` | Known Jabra 510 quirk, reported on many Linux PCs | Ignore it if recording and playback work |
| `wpctl`: `Could not connect to PipeWire` | No user session / services not running | Redo step 3, then `systemctl --user status pipewire wireplumber` |
| Jabra in `/proc/asound/cards` but not in `wpctl status` | WirePlumber not running | `systemctl --user restart wireplumber` |
| Recording is silent | Jabra muted (red LED), source muted or volume 0, wrong default source | Press the Jabra's mute button; redo step 5; check `*` in step 4 |
| Playback silent | Wrong default sink or Jabra volume at 0 | Check `*` in step 4; Jabra + button |
| `arecord -D hw:…`: `Device or resource busy` | PipeWire owns the device (expected) | Use `-D default` as in step 7 |
| Crackling / dropouts | Power or USB issue | `vcgencmd get_throttled` must be `0x0`; try the other black USB port |
| Works when logged in, not after reboot | Lingering off | `loginctl show-user "$USER" -p Linger` → must be `Linger=yes` |

## Notes for later phases

- **1.6 / 1.7 (ears):** the C++ capture code can use the ALSA API on `default` (through `pipewire-alsa`) or PipeWire's own API. Decide in 1.6. Either way, request 16 kHz mono and let PipeWire resample.
- **1.9 (Spotify):** raspotify installs librespot as a *system* service, which can't reach `dev`'s PipeWire session. Plan: run librespot as a user unit or point it at the user's PulseAudio socket. Decide in 1.9.
- **1.11:** the echo test result from step 8 decides whether we need PipeWire's echo-cancel module.
- **1.12:** WirePlumber handles re-plugging the Jabra; services must cope with the device appearing late after boot.
- **2.6 body design:** keep the Jabra's microphone away from the Pi's fan exhaust.
