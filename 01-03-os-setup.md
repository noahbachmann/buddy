# 1.3 – OS setup

> Written 2026-10-19, not yet tested on this exact kit. Items marked ⚠️ are assumptions.

## Goal

A headless Raspberry Pi OS Lite 64-bit (Debian 13 "Trixie") booting from the microSD, reachable with `ssh buddy`, fully updated, with cooling and power verified. The C++ build toolchain is installed so step 1.5 can start right away.

## Prerequisites

- Step 1.2 done: cooler fitted, Jabra in a black USB port, Pi powers on.
- PC with a microSD reader, on the same network as the Pi (not a guest Wi-Fi — those usually isolate devices).
- An SSH key pair on the PC. If you don't have one: `ssh-keygen -t ed25519 -C "dev@buddy"`.
- Wi-Fi name and password (or an Ethernet cable to the router).

## Parts / tools

| Item | Notes |
|---|---|
| 128 GB microSD | Will be completely erased |
| Raspberry Pi Imager 2.x | From raspberrypi.com/software. ⚠️ 2.0.8 was current in April 2026; take the latest. |
| Raspberry Pi OS Lite (64-bit) | Selected inside Imager, no separate download |

## Decisions in this step

| Setting | Value | Why |
|---|---|---|
| Hostname | `buddy` | Reachable as `buddy.local`. Also becomes the MQTT broker host for LAN services (D3). |
| Username | `dev` ⚠️ change if you like | systemd units and paths will reference it later. Pick once. |
| SSH | Public key only | No passwords over the network |
| Raspberry Pi Connect | Off | Cloud remote access, not needed |
| Localisation | Bern / Europe/Zurich / `ch` keyboard | Also sets the Wi-Fi country (regulatory domain). A wrong or missing country can keep Wi-Fi disabled. |

## Steps

### 1. Flash the microSD (on the PC)

1. Put the microSD into the PC's reader and start Raspberry Pi Imager.
2. **Device:** Raspberry Pi 5.
3. **OS:** Raspberry Pi OS (other) → **Raspberry Pi OS Lite (64-bit)**.
4. **Storage:** the microSD. Check the size (~119 GB shown for a 128 GB card). Picking the wrong drive erases it.
5. **Customisation:**

   | Sub-step | Enter |
   |---|---|
   | Hostname | `buddy` |
   | Localisation | Capital city Bern, time zone Europe/Zurich, keyboard `ch` |
   | User | Username `dev`, a strong password (needed for `sudo`) |
   | Wi-Fi | SSID + password (set it even if you start on Ethernet) |
   | Remote access | Enable SSH → **public key authentication** → paste the contents of `~/.ssh/id_ed25519.pub` |
   | Raspberry Pi Connect | Leave disabled |

6. Write. Imager writes, then verifies the card. Wait until it says you can remove it.

### 2. Insert the card and boot

1. Pi unplugged. Turn the Pi over: the microSD slot is on the underside at the short edge.
2. Slide the card in with the **contacts facing the board** (label facing away) until it sits flush. Don't force it.
3. Plug in the USB-C power.
4. Wait **2–3 minutes** ⚠️. The first boot expands the filesystem and applies your Imager settings; it may reboot once by itself. The green LED flickers with disk activity.

### 3. Connect over SSH (on the PC)

Add this to `~/.ssh/config`:

```
Host buddy
    HostName buddy.local
    User dev
    IdentityFile ~/.ssh/id_ed25519
```

Then:

```bash
ssh buddy
```

Expected: a host-key fingerprint prompt (answer `yes`), then a prompt like `dev@buddy:~ $`.

### 4. Check the OS

```bash
grep PRETTY_NAME /etc/os-release   # PRETTY_NAME="Debian GNU/Linux 13 (trixie)"
uname -m                           # aarch64
uname -r                           # 6.18.x-... (or newer)
```

### 5. Update everything

```bash
sudo apt update && sudo apt full-upgrade -y
sudo reboot
```

Reconnect with `ssh buddy` after ~30 s.

### 6. Update the bootloader firmware

The Pi 5 keeps its bootloader in an EEPROM chip on the board. Updates fix power, USB and boot issues.

```bash
sudo rpi-eeprom-update
```

Expected: `CURRENT` and `LATEST` dates. If it says an update is available:

```bash
sudo rpi-eeprom-update -a
sudo reboot
```

### 7. Health check: power and cooling

**Power** — has the Pi ever seen too little voltage?

```bash
vcgencmd get_throttled             # throttled=0x0
```

`0x0` means no undervoltage and no throttling since boot. Anything else → see Troubleshooting.

**Supply detected as 5 A** (this unlocks the 1.6 A USB budget):

```bash
od -An -tu4 --endian=big /proc/device-tree/chosen/power/max_current   # 5000  ⚠️
```

**Cooling** — load all four cores for 2 minutes and watch temperature and fan:

```bash
sudo apt install -y stress-ng
stress-ng --cpu 4 --timeout 120s &
watch -n 2 'vcgencmd measure_temp; vcgencmd get_throttled; cat /sys/devices/platform/cooling_fan/hwmon/hwmon*/fan1_input'
```

Expected ⚠️: the fan RPM rises from 0 to several thousand, the temperature levels off (roughly 60–70 °C), and `throttled` stays `0x0`. Press `Ctrl+C` after the stress test ends.

*Why this matters:* speech-to-text later loads the CPU exactly like this. If the cooler isn't seated well, you'll see the temperature climb past 80 °C and throttling flags appear.

### 8. Check the Jabra is visible

Audio setup is step 1.4. For now, just confirm the USB device:

```bash
lsusb | grep -i -E 'jabra|gn netcom'
```

Expected: one line naming the Jabra / GN Netcom device.

### 9. SSH: keys only

Check whether password logins are still allowed:

```bash
sudo sshd -T | grep -i '^passwordauthentication'
```

If it says `yes`:

```bash
echo 'PasswordAuthentication no' | sudo tee /etc/ssh/sshd_config.d/10-no-password.conf
sudo systemctl reload ssh
```

Test from a **second** terminal that `ssh buddy` still works before closing the first one.

### 10. Install the C++ build toolchain

Needed for CLion Full Remote Mode in step 1.5 (D5/D6), independent of the systemd/Docker decision.

```bash
sudo apt install -y build-essential cmake ninja-build gdb rsync git ccache pkg-config
gcc --version | head -1     # gcc (Debian 14.x...) ⚠️
cmake --version | head -1   # cmake version 3.31+ ⚠️
```

### 11. Shut down cleanly

```bash
sudo poweroff
```

Wait until the green LED stops, then unplug.

## Verify

- [ ] `ssh buddy` works with your key, without a password prompt.
- [ ] Debian 13 (trixie), `aarch64`.
- [ ] `rpi-eeprom-update` shows up to date.
- [ ] `vcgencmd get_throttled` → `0x0`, also after the stress test.
- [ ] Fan spins up under load.
- [ ] Jabra listed by `lsusb`.
- [ ] `gcc`, `cmake`, `gdb` installed.

**Done when:** `ssh buddy.local` works. ✅

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `Could not resolve hostname buddy.local` | mDNS not answering yet, or PC on another network | Wait another minute. Find the Pi's IP in your router's device list and use `ssh dev@<ip>`. Make sure PC and Pi are on the same (non-guest) network. |
| `Permission denied (publickey)` | Wrong key or username in Imager | Check `User` and `IdentityFile` in `~/.ssh/config`. If wrong in Imager: reflash. |
| Pi not on Wi-Fi | Wi-Fi country missing/wrong, typo in password | Connect Ethernet, then `nmcli dev wifi list` and `nmcli dev status` |
| Green LED repeats a blink pattern, no SSH | Card not readable | Reseat the card. Reflash with verification. |
| `WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED` | You reflashed the card | `ssh-keygen -R buddy.local` |
| `throttled` not `0x0` | Undervoltage (power) or overheating | Use the 27 W supply directly, no extension adapters. Re-check the cooler seating (guide 1.2, step 4). |
| `max_current` file missing or not 5000 | Supply not negotiated as 5 A | Check it's the official 27 W supply ⚠️ |

## Notes for later phases

- **1.5 workflow:** CLion's Full Remote Mode syncs sources into a folder under `/tmp` on the Pi by default. ⚠️ On Debian 13, `/tmp` is a RAM disk: fast and no microSD wear, but gone after a reboot (first build after reboot is a full rebuild). We decide in 1.5 whether to move it.
- **Username `dev`** will appear in systemd units and paths.
- **Hostname `buddy`:** services running on another LAN machine use `buddy.local` as MQTT broker address.
- **Backup:** once 1.5 is done, the Pi should be rebuildable from a setup script in `deploy/`, so a dead microSD costs minutes, not days.
