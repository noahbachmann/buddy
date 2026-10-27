# 2.2 – Motion controller

> Items marked ⚠️ are untested assumptions: check them while you build and tell Claude what you found.

## Goal

A Raspberry Pi Pico 2 on the Pi's USB drives hobby servos from its own hardware PWM. A separate 5 V supply powers the
servos. The Pi sends text commands over USB serial; the Pico moves the servos smoothly, keeps each servo inside its
limits, and lets go of all servos when the Pi goes quiet.

```
 Pi 5 ──USB (data + Pico power)──► Pico 2 H ──GP2/GP3 signal (3.3 V)──► servos
                                     │ GND                                ▲ 5 V + GND
                                     └────────── shared ground ───────────┤
                              5 V brick ──► servo rail (+ 1000 µF) ───────┘
```

**Done when** (all from the Pi, step 9):

- `MOVE` sweeps a servo, and a move past its limit is clamped, not executed.
- With no input for 2 s, every servo goes limp.
- While both servos move under load, `vcgencmd get_throttled` stays `0x0` and the servo rail stays ≥ 4.8 V.

## Prerequisites

- Phase 1 done: Pi reachable as `buddy`, `deploy/deploy.sh` works.
- A free **black USB 2 port** on the Pi. The Jabra uses the other one; the blue USB 3 ports work too.

## Why it is built this way (D17)

| Choice | Why |
|---|---|
| Pico instead of the Pi's GPIO | Linux isn't real-time, so a busy Pi makes servos jitter. The Pico's PWM hardware produces exact pulses whatever the Pi does, and the Pi's GPIO header stays free. |
| Pico PWM instead of a PCA9685 | The Pico 2 (RP2350A) has 8 PWM slices × 2 outputs = **16 independent servo outputs**. GPIO16+ reuse slices 0–7. That covers head (2), hand (5) and arm (~3) with room to spare. One board less, no I2C, nothing to solder. |
| Pico 2 **H** | "H" = headers already soldered: it plugs straight into a breadboard. |
| USB serial | One cable carries power and data for the Pico. It shows up as `/dev/ttyACM0` with no drivers, and the Pi can reflash it over the same cable. |
| Limits in the firmware | A bug in the Pi service or a bad MQTT message can't drive a joint into its end stop and strip the gears or crack a print. |
| Separate servo supply | **Golden rule 1**, explained in step 1. |

## Parts & tools

Galaxus/Digitec share one catalogue, so the links work in both shops. Prices are approximate (CHF, Oct 2026); ⚠️ means
the price or stock wasn't visible when this was written.

| # | Item | Qty | ≈ CHF | Why | Link |
|---|---|---|---|---|---|
| 1 | Raspberry Pi Pico 2H (RP2350, pre-soldered headers, micro-USB) | 2 | 2 × 8 ⚠️ | One spare: a board killed by a wiring mistake shouldn't stop the project | [Digitec 58582559](https://www.digitec.ch/en/s1/product/raspberry-pi-pico-2h-development-boards-kits-58582559) |
| 2 | USB-A → micro-USB B cable, 0.5 m, USB 2.0 (**data**, not charge-only) | 1 | 7 ⚠️ | Pi ↔ Pico | [Galaxus 14290277 (ACT)](https://www.galaxus.ch/de/s1/product/act-usb-20-a-male-micro-b-male-050-m-usb20-cable-am-microbm-050m-050-m-usb-20-usb-kabel-14290277) |
| 3 | 5 V / 5 A power supply, 5.5 × 2.1 mm plug | 1 | 20 ⚠️ | Servo power only. 5 A covers head + hand (step 1) | [Digitec 36195725 (Purecrea)](https://www.digitec.ch/de/s1/product/purecrea-5v-dc-5000ma-stecknetzteil-acdc-adapter-55mm21mm-stecker-entwicklungsboard-kit-36195725) |
| 4 | DC socket 5.5 × 2.1 mm with screw terminals | 1 | 9.90 | Supply → wires without soldering | [Galaxus 34239719 (Purecrea)](https://www.galaxus.ch/fr/s1/product/purecrea-connecteur-femelle-dc-barrel-jack-55mm-21mm-avec-bornes-a-vis-cable-prise-electronique-34239719) |
| 5 | Breadboard 830 + ~140 pre-cut jumper wires | 1 | 15 ⚠️ | Prototype wiring | [Galaxus 5998042 (OEM combo)](https://www.galaxus.ch/en/s1/product/oem-breadboard-with-plug-in-cable-combo-breadboard-electronics-supplies-casing-5998042) |
| 6 | Jumper wires M/M 30 cm, 22 AWG | 1 × 10 | 5 ⚠️ | Thicker wires for the 5 V / GND supply leads | [Digitec 5997881](https://www.digitec.ch/de/s1/product/oem-jumperkabel-10-stk-mm-30cm-22awg-elektronikkabel-stecker-5997881) |
| 7 | Male pin header 1 × 40, 2.54 mm, straight | 1 pack | 4 ⚠️ | Break off 3-pin pieces: the servo plug pushes onto them in the breadboard | [Digitec 5997724](https://www.digitec.ch/de/s1/product/oem-stiftleiste-1-x-40-polig-rastermass-254mm-schwarz-elektronikkabel-stecker-5997724) |
| 8 | Electrolytic capacitor 1000 µF, 25 V, radial | 2 | 2 × 2 ⚠️ | Buffers the servos' current spikes (step 1) | [Digitec 5848462 (Velleman)](https://www.digitec.ch/de/s1/product/velleman-elco-radial-1000f-25v-kondensator-5848462) |
| 9 | Resistor assortment 1/4 W, 1 % (330 Ω used now) | 1 | 19 | One per servo signal line; the rest is for the display (2.4) and later | [Galaxus 25475139 (Purecrea 600 pcs)](https://www.galaxus.ch/de/s1/product/purecrea-widerstand-set-600-stk-assortiert-14w-1-genauigkeit-widerstand-25475139) |
| 10 | MG90S metal-gear micro servo, 180° | 4 | 4 × 9 ⚠️ | 2 for this step and the 2.5 pan/tilt, 2 spares or hand tests | [Galaxus 24962704 (Waveshare)](https://www.galaxus.ch/en/s1/product/waveshare-micro-servo-mg90s-mit-metallgetriebe-motor-electronics-modules-24962704) |
| 11 | Multimeter UNI-T UT33C+ | 1 | 20 | Measure the supply and the rail under load; you need it for every hardware step from here on | [Galaxus 25236531](https://www.galaxus.ch/de/s4/product/unit-multimeter-uni-t-ut33c-hand-multimeter-multimeter-25236531) |
| | **Total** | | **≈ 160** | | |

Notes on the choices:

- **Power supply (#3):** I'd prefer a branded supply (the Mean Well GST60A05-P1J, 5 V 6 A desktop, from Distrelec ⚠️), but
  Galaxus doesn't list it. The Purecrea is the Galaxus option with a confirmed 5.5 × 2.1 plug. Check its polarity
  (centre +) with the multimeter in step 2 before connecting anything. Don't use a 5 V USB-C PD charger: they only
  deliver ~3 A at 5 V.
- **MG90S (#10):** buy the 180° version, not the "360°" continuous-rotation one, which can't hold a position. Coscorp's
  "MG90" is a different, weaker servo. ⚠️ MG90S stall current is ~0.7 A at 5 V according to sellers, with no
  manufacturer datasheet. Torque is 2.0 kg·cm, fine for a light head; re-check against the 2.6 head weight.
- **Not needed now:** soldering iron (2.4 or 3.x), a PCA9685, logic-level shifters (the MG90S accepts the Pico's 3.3 V
  signal ⚠️).

Fallbacks if Galaxus is out of stock: Bastelgarage (most Purecrea items are theirs) and pi-shop.ch (Pico 2H).

## Steps

### 1. Electrical basics: why the wiring looks like this

**Why servos never get power from the Pi (golden rule 1).** A servo's motor pulls ~10 mA standing still, 200–400 mA
moving, and **~0.7 A when stalled**. A servo stalls whenever it's blocked or starting a fast move. Two servos starting
together pull ~1.5 A for a few milliseconds. The Pi's USB ports share a 1.6 A budget, and the Jabra already uses part of
it. A spike like that drops the Pi's supply voltage: undervoltage throttling, USB audio dropouts, or a reset that
corrupts the microSD. So the servos get their own supply, and the Pi only ever sends the signal wire.

**Supply size.** Head 2 × 0.7 A + hand 5 × 0.7 A = 4.9 A worst case, if everything stalls at once (it won't, but the
supply must not collapse when several do). 5 A is enough through Phase 3; the stronger arm servos (Phase 4) get their own
supply decision.

**Why the grounds must be connected.** The servo reads its signal as "voltage between the signal wire and *its* GND".
The Pico produces the signal relative to *its* GND. Without a wire between the two GNDs there is no common reference: the
servo jitters or ignores the signal. So: supply −, servo GND and Pico GND all meet on the breadboard's ground rail. The
**+** sides never meet: servo 5 V must not touch the Pico's VBUS (pin 40) or VSYS (pin 39). VBUS is wired straight to the
Pi's USB 5 V, so servo current would flow back into the Pi.

**Why the capacitor.** The supply sits a metre of thin wire away. When a servo starts, its current rises faster than
that wire can deliver, and the rail dips for a moment. The 1000 µF capacitor next to the servos is a small local
reservoir that covers those spikes. It's **polarised**: the leg under the stripe (−, also the shorter leg) goes to GND.
Reversed, an electrolytic capacitor heats up and can burst.

**Why the 330 Ω resistor in the signal line.** If a servo is plugged in wrong, or its motor sends voltage spikes back up
the signal wire, the resistor limits the current into the Pico's pin to a harmless level. The servo's input barely draws
current, so the resistor doesn't change the signal.

**3.3 V signal.** The Pico's pins are 3.3 V, the servo runs on 5 V. Hobby servos treat anything above ~2.5 V as "high",
so 3.3 V works without a level shifter ⚠️ (true for the MG90S according to many hobby reports; you confirm it in step 9).

### 2. Check the supply with the multimeter

The multimeter is new to you, so the setup is: black lead in **COM**, red lead in **VΩ** (never the **10A** socket for
voltage), dial on **V⎓** (DC volts; on the UT33C+ the 20 V range).

1. Screw two 22 AWG jumper wires into the DC socket: red into **+**, black into **−**. Keep the ends apart.
2. Plug in the supply, plug its barrel into the socket.
3. Red probe on the red wire, black probe on the black wire. Expected: **5.0–5.3 V**. A reading of **−5 V** means the
   polarity is reversed: swap the wires in the terminals and mark the socket.
4. Unplug the supply from the wall before wiring anything (step 3).

### 3. Breadboard wiring

Breadboard basics: the long rails along the edges (red +, blue −) are connected along their full length. The short
5-hole rows in the middle are connected across, not along. The centre gap separates the two halves.

Pinout (Pico 2 pin numbers are printed on the underside; pin 1 is next to the USB connector, top left):

| Pico pin | Signal | Goes to | Note |
|---|---|---|---|
| 3 (GND) | GND | ground rail (−) | **Shared ground**, see step 1 |
| 4 (GP2) | servo ch0 = head pan | 330 Ω → servo 0 signal (orange) | |
| 5 (GP3) | servo ch1 = head tilt | 330 Ω → servo 1 signal (orange) | |
| 6–12 (GP4–GP9) | servo ch2–ch7 | reserved: hand (Phase 3) | |
| 14–17, 19–20 (GP10–GP15) | SPI1 | reserved: round display (2.4) ⚠️ final in 2.4 | |
| 39 (VSYS), 40 (VBUS) | — | **nothing** | Never connect servo 5 V here |
| USB | power + data | Pi | The Pico's only power source |

1. Pico 2H straddling the centre gap at one end of the breadboard, USB connector pointing outwards.
2. Pico pin 3 (GND) → ground rail (−) with a short jumper.
3. Supply leads from the DC socket: red → **+ rail on the far side** (the servo rail), black → ground rail. If the
   breadboard's two long sides aren't bridged, bridge the two **− rails only** with one jumper; the + rails stay separate.
4. Capacitor straight across the servo rail, as close to the servo headers as possible. Stripe leg into −.
5. For each servo: a 3-pin header piece in three rows. Row 1 gets a jumper to −, row 2 a jumper to + (servo rail),
   row 3 gets a 330 Ω resistor from the Pico pin (GP2 / GP3). The MG90S plug colours: **brown = GND, red = 5 V,
   orange = signal**. Plug it on so brown sits on the − row.
6. Before powering: multimeter on **Ω** (or the continuity beep), supply unplugged. Check that servo rail + and Pico
   pin 39/40 show **no** connection, and that rail − and Pico pin 3 **do**.

Power-up order (from now on, every time): Pico USB first, then the servo supply. Power-down: servo supply first.
Reason: a servo with power but no signal sits still. A Pico whose pins see voltage before it has its own power is driven
through its pins.

### 4. Pi setup: toolchain, picotool, udev (new `setup.sh pico` step)

Claude adds a `pico` step to `deploy/pi/setup.sh` (and to its `ALL` list). Run it with
`ssh buddy 'bash ~/buddy/src/deploy/pi/setup.sh pico'` after a `deploy/deploy.sh` has synced the sources.

| What | How | Why |
|---|---|---|
| ARM bare-metal compiler | apt: `gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib` (trixie: GCC 14.2) | builds the firmware on the Pi |
| picotool 2.3.1 | built from source (pinned, like libpiper) → `/opt/picotool`; apt: `libusb-1.0-0-dev` | Debian ships 2.1.1, older than the SDK. The SDK needs picotool to make the UF2 and the Pi uses it to flash |
| Pico SDK 2.3.1 | fetched by the firmware's CMake at the pinned tag, not installed | same pattern as whisper.cpp (D9) |
| picocom | apt | manual serial tests |
| udev rule `/etc/udev/rules.d/60-buddy-pico.rules` | `SUBSYSTEM=="tty", ATTRS{idVendor}=="2e8a", ATTRS{product}=="Buddy motion", SYMLINK+="buddy-pico"` + picotool's own rules file for BOOTSEL access as `dev` | stable `/dev/buddy-pico` instead of `ttyACM0/1` (the order depends on boot timing); flashing without sudo |

`dev` is already in the `dialout` group on Raspberry Pi OS (check: `id dev`), which owns `/dev/ttyACM*`.

### 5. Firmware: `skeleton/firmware/pico/`

The firmware lives inside `skeleton/` so `deploy.sh` syncs it and it can share the protocol header with the motion
service. It's a **separate CMake project**: the Pico SDK sets its own toolchain before `project()`, so it can't be an
`add_subdirectory` of the main build.

```
skeleton/firmware/pico/
├── CMakeLists.txt     ← PICO_BOARD=pico2, PICO_PLATFORM=rp2350-arm-s, pico-sdk 2.3.1 via FetchContent,
│                        USBD_PRODUCT="Buddy motion", stdio over USB only (no UART)
├── main.cpp           ← main loop: read lines, run ramps at 50 Hz, link watchdog, hardware watchdog
├── servo.hpp/.cpp     ← channel table (ch → GPIO), PWM setup, limits, ramp state
└── board.hpp          ← pin map from step 3
skeleton/shared/include/buddy/motion_protocol.hpp  ← parse/format, shared with services/motion (2.3)
```

Behaviour:

| Topic | Behaviour | Why |
|---|---|---|
| PWM | 150 MHz / 150 = 1 MHz tick, wrap 19 999 → 50 Hz; the compare value *is* the pulse in µs | Simple and exact: no conversion maths |
| Boot | All channels **disabled** (no pulses); a channel needs `LIMIT` before it accepts `MOVE` | A fresh Pico never moves anything on its own |
| Limits | Per channel `min_us`/`max_us` within a hard 500–2500 µs; `MOVE` outside gets clamped, and the reply says so | Mechanical safety stays on the Pico even if the Pi misbehaves |
| Ramps | Linear from the current to the target pulse over `ms`, updated every 20 ms (one PWM period) | Smooth moves without the Pi streaming positions |
| Link watchdog | No line received for **2 s** → relax all channels, report `* RELAXED link` | Pi crashed or service stopped: servos go limp instead of holding against an obstacle |
| Hardware watchdog | `watchdog_enable(500 ms)`, fed by the main loop | Firmware hang → the Pico reboots → channels disabled |
| "Relax" | PWM output off (pin low) | A servo without pulses stops driving its motor: limp, no current |

⚠️ When a channel gets its first pulse after power-up, the servo jumps to that position at full speed: it doesn't know
where it is. For the head this is harmless; for the hand and arm (Phase 3/4), the motion service sends the *expected*
rest position first.

### 6. Protocol v1

Text lines (`\n`-terminated, ASCII) over USB CDC. USB already checksums and retransmits, so no framing or CRC is
needed, and you can type commands by hand in picocom.

Every request starts with an id (1–65535) that the reply echoes, so the service matches replies to requests:

| Request (Pi → Pico) | Reply | Meaning |
|---|---|---|
| `<id> HELLO` | `<id> OK buddy-motion <fw-version> 1` | Handshake; the last number is the protocol version |
| `<id> LIMIT <ch> <min_us> <max_us>` | `<id> OK` | Enable channel with limits (does not move it) |
| `<id> MOVE <ch> <us> <ms>` | `<id> OK` / `<id> OK clamped <us>` | Ramp to the pulse over `ms` (0 = jump) |
| `<id> RELAX <ch>` / `<id> RELAX *` | `<id> OK` | Stop pulses on one / all channels |
| `<id> PING` | `<id> OK` | Keeps the link watchdog happy (the service sends one every 500 ms) |
| any error | `<id> ERR <reason>` | `bad-channel`, `no-limit`, `bad-args`, `unknown` |

| Unsolicited (Pico → Pi) | Meaning |
|---|---|
| `* DONE <ch> <us>` | A ramp reached its target |
| `* RELAXED link` | The link watchdog fired |
| `* BOOT <fw-version>` | The firmware just started (after a reset, a watchdog reboot or a flash) |

`motion_protocol.hpp` holds parsing and formatting for both sides: plain C++20, no heap and no exceptions (the firmware
builds with `-fno-exceptions`). The host tests it with GoogleTest in `shared/tests/test_motion_protocol.cpp` (suite
`MotionProtocol`: round trips, malformed lines, clamping), run by `deploy/test.sh`.

### 7. Build and flash

Claude adds `deploy/deploy.sh --firmware`. It syncs, then on the Pi:

```bash
cmake -S ~/buddy/src/firmware/pico -B ~/buddy/build-pico -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build ~/buddy/build-pico
/opt/picotool/bin/picotool load -x -f ~/buddy/build-pico/buddy-motion.uf2
```

`-f` asks the running firmware to reboot into the bootloader (works because the SDK's USB stdio has a reset interface),
`-x` starts the new firmware after flashing. Expected end of the output: `Loading into Flash: [====] 100%` and
`The device was rebooted to start the application.`

**First flash (and recovery):** a brand-new or crashed Pico has no firmware that can be asked to reboot. Hold the
**BOOTSEL** button, plug in the USB cable, release. The Pico now shows up as a USB drive in bootloader mode, and the same
`picotool load -x` (without `-f`) flashes it. `ssh buddy picotool info` should then list `buddy-motion`.

The first CMake configure downloads the SDK (~200 MB with submodules: TinyUSB is needed) into the build dir; later builds
are incremental.

### 8. Check the device

```bash
ssh buddy 'ls -l /dev/buddy-pico; dmesg | tail -5'
```

Expected: `/dev/buddy-pico -> ttyACM0` (or `ttyACM1`), and dmesg shows `Product: Buddy motion`, `cdc_acm … ttyACM0`.

### 9. Manual test with picocom

Servo supply on. Then:

```bash
ssh -t buddy picocom --echo --omap crlf /dev/buddy-pico
```

Type (Enter after each line; exit with Ctrl-A Ctrl-X):

```
1 HELLO                → 1 OK buddy-motion 0.1.0 1
2 LIMIT 0 1000 2000    → 2 OK                      (servo doesn't move yet)
3 MOVE 0 1500 0        → 3 OK                      (jumps to centre)
4 MOVE 0 1000 1000     → 4 OK, then * DONE 0 1000  (turns slowly for 1 s)
5 MOVE 0 2400 500      → 5 OK clamped 2000         (stops at the limit)
```

Now wait 2 s without typing: `* RELAXED link`, and the servo turns freely by hand.

Load test (second terminal, while you type MOVEs for ch0 and ch1 with `0` ms, back and forth):

```bash
ssh buddy 'watch -n 0.5 vcgencmd get_throttled'
```

Multimeter on V⎓ across the servo rail (at the capacitor) while both servos move. Hold one horn lightly with your
fingers to load it.

## Verify

| Check | Expected |
|---|---|
| `MOVE` with a ramp | Smooth movement, `* DONE` at the end |
| `MOVE` past the limit | `OK clamped`, the servo stops at the limit |
| 2 s without input | `* RELAXED link`, servos limp |
| `vcgencmd get_throttled` during the load test | `0x0` throughout |
| Servo rail under load | ≥ 4.8 V (MG90S spec minimum) |
| Pico unplugged and replugged | `/dev/buddy-pico` comes back, `* BOOT …` on connect |
| `deploy/test.sh -R MotionProtocol` | passes |

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| Servo jitters or twitches at rest | Missing common ground (step 3.2/3.3), or the signal runs next to the supply leads; shorten wires |
| Servo jitters only while the other moves, rail < 4.8 V | Supply too weak or leads too thin/long: use the 22 AWG leads, capacitor next to the servos |
| `get_throttled` ≠ `0x0` during servo tests | Servo current reaches the Pi: check that nothing connects servo + to Pico pin 39/40. Unplug the servo supply first |
| No `/dev/buddy-pico`, `lsusb` shows no `2e8a` | Charge-only cable (no data wires): use the listed cable. Or no firmware yet: BOOTSEL flash (step 7) |
| `lsusb` shows `2e8a:000f` but no tty | Pico is in bootloader mode (RP2350 BOOTSEL): flash it (step 7) |
| `/dev/ttyACM0` exists but not `/dev/buddy-pico` | udev rule missing or product string differs: `udevadm info -a /dev/ttyACM0 \| grep product` |
| picotool: `No accessible RP-series devices` / permission denied | picotool's udev rules not installed: `setup.sh pico`, then replug |
| `picotool load -f` can't reboot the Pico | Running firmware hung or built without the USB reset interface: BOOTSEL flash |
| Servo hums at an end stop | Limits too wide for this servo: narrow `LIMIT` (2.5 calibrates them) |
| Servo turns continuously | You have a 360° (continuous) MG90S: wrong variant |

## Notes for later phases

- **2.3:** `services/motion` owns `/dev/buddy-pico`: picocom and the service can't both have the port open. The service
  sends the `LIMIT`s on every connect (the Pico forgets them on reset).
- **2.4:** the display gets SPI1 on GP10–GP15. Whether the Pico or the Pi drives it is a 2.4 decision; the reserved pins
  keep both options open.
- **2.5:** limits and centre per servo get calibrated with the head mechanics: `LIMIT` values in µs per joint.
- **2.6:** the breadboard becomes a soldered perfboard or a small custom PCB inside the body: first soldering. Keep the
  servo rail and Pico on one board with short supply leads.
- **Phase 3:** 5 hand servos on GP4–GP8; the 5 A supply covers them (step 1). Check the rail voltage with all 7 moving.
- **Phase 4:** arm servos (MG996R-class, ~2.5 A stall each) need their own supply and probably 6 V: a new decision then.
- **When the parts arrive:** add a "Motion" section to `buddy-hardware.md` (Pico 2H, supply model, servo type).
