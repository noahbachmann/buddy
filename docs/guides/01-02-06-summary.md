# 1.2 – 1.6 Summary (done)

Condensed from the step guides 1.2–1.6 (full versions: `git log --all -- guides/01-02-assembly.md` etc.). Everything still needed to
rebuild the Pi, re-tune or debug is here; walkthrough text and one-time checks are gone.

## 1.2 Assemble the Pi

- **Active Cooler is permanent**: push pins + thermal pads degrade on removal. Fan cable in the 4-pin **FAN** header
  (remove its plastic cap first), clear of the blades. Board supported from below while pressing the pins.
- **Jabra in a black USB 2 port** (blue USB 3 stays free). USB only, no Bluetooth.
- **27 W supply** (5.1 V / 5 A) unlocks the 1.6 A USB budget (else 600 mA). Check: `od -An -tu4 --endian=big /proc/device-tree/chosen/power/max_current` → `5000`.
- **GPIO header stays free**: motion goes through the Pico over USB (2.2). Bare board: no metal near the GPIO pins (5 V next to GND).
- **Shutdown rule**: `sudo poweroff`, wait for the green LED to stop, then unplug. Pulling power while the OS runs risks the microSD.
- Later: 2.6 housing needs an intake above the blower and a vent at its side exhaust, PETG HF near the Pi; keep the Jabra
  mic away from the exhaust. Camera (4.2) uses a CAM/DISP flat-cable connector.

## 1.3 OS setup

| Imager setting | Value |
|---|---|
| Device / OS | Raspberry Pi 5 / Raspberry Pi OS Lite (64-bit) = Debian 13 trixie |
| Hostname / user | `buddy` / `dev` (used in units and paths) |
| Localisation | Bern, Europe/Zurich, keyboard `ch` (also sets the Wi-Fi country) |
| SSH | Public key only; Raspberry Pi Connect off |

PC `~/.ssh/config`: `Host buddy` → `HostName buddy.local`, `User dev`, `IdentityFile ~/.ssh/id_ed25519`.

After first boot: `sudo apt update && sudo apt full-upgrade -y`, `sudo rpi-eeprom-update -a`, reboot.
Password SSH off: `echo 'PasswordAuthentication no' | sudo tee /etc/ssh/sshd_config.d/10-no-password.conf && sudo systemctl reload ssh`.

Health checks (also useful later, see `deploy/status.sh`):

```bash
vcgencmd get_throttled     # 0x0 = never undervolted/throttled since boot
stress-ng --cpu 4 --timeout 120s & watch -n 2 'vcgencmd measure_temp; vcgencmd get_throttled'   # levels off ~60–70 °C
```

| Symptom | Fix |
|---|---|
| `buddy.local` doesn't resolve | Same non-guest network? Use the IP from the router meanwhile |
| `REMOTE HOST IDENTIFICATION HAS CHANGED` after reflashing | `ssh-keygen -R buddy.local` |
| `throttled` ≠ `0x0` | 27 W supply directly, no adapters; re-seat the cooler |
| No Wi-Fi | Ethernet, then `nmcli dev wifi list` / `nmcli dev status` |

## 1.4 Audio

- **PipeWire runs per user**, so the `dev` session must exist without a login: **lingering** (`sudo loginctl enable-linger dev`).
  Packages: `pipewire pipewire-alsa pipewire-pulse wireplumber alsa-utils`;
  `systemctl --user enable --now pipewire.socket pipewire-pulse.socket wireplumber.service`.
- Jabra must be the default **sink and source** (`*` in `wpctl status`); set with `wpctl set-default <id>`, WirePlumber
  remembers it by device name. Volumes: sink 0.6, source 1.0 unmuted (`wpctl set-volume/set-mute @DEFAULT_AUDIO_SOURCE@ …`).
- Speech format everywhere: **16 kHz, mono, s16**. Test: `pw-record --rate 16000 --channels 1 --format s16 x.wav` / `pw-play x.wav`;
  ALSA route: `arecord -D default -f S16_LE -r 16000 -c 1 -d 5 x.wav`. Programs use ALSA `default` (= PipeWire), never `hw:`.
- Echo test (input for 1.11): pink noise through the speaker (`speaker-test -c 2 -t pink -l 0`) while recording.

| Symptom | Fix |
|---|---|
| Jabra missing in `/proc/asound/cards` | Other black USB port; `lsusb`; `dmesg \| tail` after plugging in |
| `cannot set freq 48000 to ep 0x3` in dmesg | Known Jabra 510 quirk: ignore if audio works |
| `Could not connect to PipeWire` / works only when logged in | Lingering off (`loginctl show-user dev -p Linger`) or user services not enabled |
| Jabra in cards but not in `wpctl status` | `systemctl --user restart wireplumber` |
| Silent recording | Jabra mute LED red, source muted, wrong default source |
| `arecord -D hw:…` busy | Expected (PipeWire owns it): use `-D default` |
| Crackling | `vcgencmd get_throttled`; other USB port |

## 1.5 Skeleton, MQTT & workflow

Decisions now live in CLAUDE.md (Code conventions, D2, D6, D7). Repo root for all commands: `skeleton/`.

| Pi path | Content |
|---|---|
| `~/buddy/dev/` | CLion's synced copy + Debug build |
| `~/buddy/src/`, `~/buddy/build-release/` | `deploy.sh`'s copy and Release build (ccache) |
| `~/buddy/bin/`, `~/buddy/models/` | Installed binaries, models (not in Git) |
| `~/buddy/config/buddy.env`, `soloist.env`, `spotify-token.json` | Runtime config + secrets, mode 600, not in Git |
| `~/.config/systemd/user/` | `buddy@.service` template + Soloist units |

**CLion Full Remote Mode** (Dev's PC):
1. Toolchains → `+` Remote Host `buddy-pi`: host `buddy.local`, user `dev`, auth via OpenSSH config/agent; CMake, GCC, GDB get green checks.
2. CMake Debug profile: toolchain `buddy-pi`, generator Ninja, options `-DCMAKE_CXX_COMPILER_LAUNCHER=ccache`.
3. Deployment → SFTP entry → Mappings → deployment path **`/home/dev/buddy/dev`** (default is under `/tmp` = RAM disk, wiped on reboot).
4. Before Run/Debug: `ssh buddy systemctl --user stop buddy@<name>` (same MQTT client id; otherwise both flip connected/lost).

The 1.5 smoke-test services `ping`/`pong` were removed in 1.12 (git history). Bus test without them: `mosquitto_sub -v -t 'buddy/#'` in one terminal, `mosquitto_pub -t buddy/test -m hi` in another.

| Symptom | Fix |
|---|---|
| CMake can't find `PahoMqttCpp` | `apt install libpaho-mqttpp-dev` (or `deploy.sh --setup`) |
| Log flips `connected` / `connection lost` | Two copies of one service (CLion + unit): stop one |
| `Connection refused` | `sudo systemctl status mosquitto` |
| `Failed to connect to bus` from `systemctl --user` | Lingering off |
| `deploy/logs.sh` empty / permission error | `sudo usermod -aG systemd-journal dev`, log in again |
| CLion sync slow | No local rsync (Windows): falls back to SFTP; acceptable or rsync via WSL |

**Offloading ears/brain to a LAN PC (D3), not done:** Mosquitto needs a LAN listener with a password, e.g.
`/etc/mosquitto/conf.d/buddy.conf` with `listener 1883`, `allow_anonymous false`, `password_file /etc/mosquitto/passwd`;
the PC runs the service in Docker with `BUDDY_MQTT_HOST=buddy.local` + user/password.

## 1.6 Wake word

openWakeWord in C++ (`services/ears/wake_word.*`), every 80 ms chunk:

```
 1280 samples ─► melspectrogram.onnx ─► 8 mel frames      (same for every wake word)
 last 76 frames ─► embedding_model.onnx ─► 96 features      (same for every wake word; ~3.7 of 4.2 ms per chunk on the Pi)
 last 16 features ─► hey_jarvis_v0.1.onnx ─► score 0..1     (the only wake-word-specific file)
```

Trigger: score ≥ `BUDDY_WAKE_THRESHOLD` (0.5) for `BUDDY_WAKE_PATIENCE` (2) chunks in a row. Threshold up = fewer false
triggers, clearer speech needed. ONNX Runtime 1.30.0 is Microsoft's prebuilt release in `/opt/onnxruntime` (D9).
MQTT: `buddy/ears/wake {"model","score"}`, `buddy/ears/state` retained `idle|listening|transcribing|offline`.

**Offline test** (no MQTT, doesn't disturb the running unit):

```bash
pw-record --rate 16000 --channels 1 --format s16 ~/hey.wav    # "Hey Jarvis", pause, "play Spotify", 2 s silence
~/buddy/bin/buddy-ears --wav ~/hey.wav
```

**Tuning** (needs your voice): stop `buddy@ears`, run it from CLion with `BUDDY_EARS_DEBUG=1` (prints every score ≥ 0.1).
Say the wake word 10× at 1 m and 10× at 2–3 m, then play TV/podcast for a few minutes. Pick a threshold between the
real peaks and the highest noise score; set it in `buddy.env`. Soak overnight, then count:
`journalctl --user-unit buddy@ears --since "-12h" | grep -c "wake word"` (target ≤ 1 false trigger per few hours).

| Symptom | Fix |
|---|---|
| CMake: `Could NOT find ONNXRUNTIME_INCLUDE_DIR` | `/opt/onnxruntime` missing: `deploy.sh --setup`, then Reload CMake Project |
| `cannot load model …` | `bash ~/buddy/src/deploy/fetch-models.sh` |
| Scores always 0 | Jabra muted or wrong default source (1.4) |
| Triggers only close by | Threshold lower; source volume 1.0 |
| Triggers on TV/music | Threshold 0.6–0.7 or patience 3 |
| `--wav`: `need 16 kHz mono 16-bit PCM` | Re-record with the exact `pw-record` flags |

### Custom "Hey buddy" model (optional, not done yet)

Trained once in Google Colab with the community notebook
<https://colab.research.google.com/github/alfiedennen/openwakeword-colab-2026/blob/main/train_wakeword.ipynb>
(openWakeWord's own notebook has been broken on current Colab since late 2025, issue #296). No Python in the repo (D5);
`buddy-ears` loads the result unchanged via `BUDDY_WAKE_MODEL`.

1. **File → Save a copy in Drive** (pip packages are unpinned; your copy survives upstream changes).
2. Runtime: **L4 + High-RAM** (Colab Pro, 75–90 min) or free T4 (~2.5 h, tab in the foreground). A disconnect = empty VM, ~25 GB downloads again; peak disk ~35 GB.
3. Config cell "★ EDIT THESE TWO LINES ★": `TARGET_PHRASE = ['hey buddy']`, `MODEL_NAME = 'hey_buddy'`, and
   `'custom_negative_phrases': ['buddy', 'hey', 'hey body', 'hey baby', 'hey daddy', 'hey bud', 'hey muddy', 'study', 'nobody', 'somebody', 'everybody', 'anybody', 'hey buddy holly', 'a buddy', 'my buddy']`
   (drop the last three if "…my buddy" should trigger). Other values default for run 1.
4. **Run all**. After section 11, listen to 5 random clips in `/content/hey_buddy_output/hey_buddy/positive_train/*.wav`;
   odd pronunciation → add a phonetic variant to `TARGET_PHRASE`.
5. Section 15 must print input `[batch, 16, 96]`, output `[batch, 1]`. Validation recall 0.5–0.7 is normal; `fp/hr` > 1 → expect TV triggers.
   **Back up `hey_buddy.onnx`** outside Downloads (not in Git, can't be re-downloaded).
6. `scp hey_buddy.onnx buddy:buddy/models/`; offline: `BUDDY_WAKE_MODEL=hey_buddy.onnx BUDDY_EARS_DEBUG=1 ~/buddy/bin/buddy-ears --wav ~/hey-buddy.wav`
   (and must *not* trigger on the Jarvis recording).
7. Compare with Jarvis using the tuning procedure above (hits at 1 m and 2–3 m, highest TV score, soak). Switch only if at
   least as good on hits and clearly better on TV: `BUDDY_WAKE_MODEL=hey_buddy.onnx`, threshold 0.5 (0.4 if it misses
   from afar while TV stays < 0.2), restart `buddy@ears`.
8. Next runs, one change each, renamed `hey_buddy_v2.onnx` …: misses from afar → `n_samples: 5000`, `augmentation_rounds: 2`;
   TV triggers → `max_negative_weight: 3000`; specific false trigger → add it to the negatives.

If switched: give the model a download location (e.g. Gitea release + SHA-256 line in `fetch-models.sh`) and update D9.
Eyes/head (2.x) react to `buddy/ears/wake` regardless of `model`.
