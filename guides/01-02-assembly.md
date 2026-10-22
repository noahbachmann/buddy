# 1.2 – Assemble the Pi (hardware setup)

> Written 2026-10-19, not yet tested on this exact kit. Items marked ⚠️ are assumptions.

## Goal

Raspberry Pi 5 with the Active Cooler fitted, Jabra connected, placed safely on the desk, and powering on.
The microSD card stays **out** for now — it gets flashed in step 1.3 and inserted then.

## Prerequisites

- Step 1.1 done (all parts on the desk).
- A free wall socket and ~30 × 20 cm of desk space.
- For step 1.3: a PC with a microSD reader (built-in or USB). Check you have one now.
- Wi-Fi name and password, or a free Ethernet port on your router.

## Parts / tools

| Item | Used for | Notes |
|---|---|---|
| Raspberry Pi 5, 8 GB | Main computer | Hold by the edges only |
| Raspberry Pi Active Cooler | Cooling | ⚠️ Assumed this is the "official heatsink" in `buddy-hardware.md`: aluminium heatsink + small blower fan + short cable. If yours has no fan and no cable, stop and say so — step 4 changes. |
| Official 27 W USB-C power supply | Power | 5.1 V / 5 A |
| 128 GB microSD | OS storage | Leave it in its packaging until 1.3 |
| Jabra Speak 510 MS | Mic + speaker | USB cable is attached to the device |
| Non-conductive base | Standing the Pi on | Cardboard, wooden board, a book |
| *Optional:* Ethernet cable | First boot | More reliable than Wi-Fi for initial setup |
| *Optional:* micro-HDMI → HDMI cable + monitor | Debugging only | The Pi 5 has **micro**-HDMI, not full size. Not needed for headless setup. |

No tools, no soldering.

## Why these steps matter

- **Static electricity (ESD):** You can carry a few thousand volts of static charge without feeling it. Discharging it into a chip can damage it silently. Touching a grounded metal object first equalises you to ground.
- **Heat:** The Pi's processor slows itself down ("throttles") when it gets too hot. Speech-to-text (step 1.7) keeps all four cores busy for seconds at a time. Without active cooling, it throttles and every voice command gets slower.
- **Short circuits:** On a bare board, the 40-pin GPIO header has 5 V and 3.3 V pins right next to ground pins. A paperclip, screw or loose wire touching them while powered can kill the Pi instantly. This is the main risk of running without a case.

## Steps

### 1. Prepare the work spot

1. Clear the area of metal bits (screws, paperclips, jewellery, loose wires).
2. Don't work on carpet. A wooden or plastic desk is fine.
3. Touch a grounded metal object (radiator, the metal case of a plugged-in desktop PC) to discharge yourself. Repeat if you walk away and come back.

> ⚠️ Don't use the shiny silver anti-static bag as a base for a *powered* board. Its surface is slightly conductive. It protects parts in storage, not while running.

### 2. Unpack and inspect

1. Take the Pi out, holding it by the board edges. Don't touch the chips or the pins.
2. Look for bent GPIO pins, cracked parts or anything rattling. If something looks damaged, stop and return it.
3. Unpack the Active Cooler. The thermal pads (soft grey squares) are already on its underside. If they have a clear protective film on them, peel it off — the film would block heat transfer. ⚠️ Some batches ship without film.

### 3. Find your way around the board

Look for the printed labels on the board.

| Part | What it is | Used in |
|---|---|---|
| USB-C port | Power input | Now |
| 2 × micro-HDMI | Monitor outputs | Debugging only |
| 2 × USB-A, blue | USB 3 ports | Spare (fast storage later) |
| 2 × USB-A, black | USB 2 ports | Jabra now, Pico in 2.2 |
| Ethernet port | Wired network | Optional |
| 40-pin GPIO header | General-purpose pins | Stays free (Pico handles motors over USB) |
| 4-pin connector labelled **FAN** | Fan power + speed control | Now (cooler) |
| microSD slot | Boot storage | **Underside** of the board, step 1.3 |
| Small button on the board edge | Power button | Shutdown / wake |
| Two flat-cable connectors (CAM/DISP) | Camera / display | Camera in 4.2 |
| Red + green LEDs | Power / activity status | Now |

### 4. Fit the Active Cooler

This is a **one-time** installation. The cooler is not designed to be removed — the push pins and pads degrade if you pull it off. Take your time.

1. Make sure nothing is plugged into the Pi.
2. **Remove the small plastic cover from the FAN connector.** It's a tiny cap that protects the pins. Pull it straight up (fingernail or gently with tweezers). Do this first — it's hard to reach once the cooler is on.
3. Hold the cooler above the board so its two white push pins line up with the two dedicated mounting holes next to the processor, and the fan cable points toward the FAN connector.
4. Support the board from underneath with your fingers directly below the two holes. *Why:* so the board doesn't bend. Bending can crack the tiny solder joints under the processor.
5. Press down **evenly on both pin tops at the same time** until they click and the cooler sits flat. Don't press on the fan itself.
6. Check: gently try to wiggle the cooler. It should not move. If one side lifts, press that pin again.
7. Plug the fan cable into the FAN connector. The plug only fits one way round — look at the shape, line it up, then push straight down until fully seated. Never force it, and never plug it into the GPIO header.
8. Tuck the cable so it can't touch the fan blades.

### 5. Place the Pi

1. Put the Pi on your non-conductive base.
2. Keep the side where the fan blows air out unobstructed (a few cm free). The blower pulls air in from the top and pushes it out sideways.
3. Keep anything metal away from the GPIO header.
4. Put the Jabra next to the Pi, not on top of it. *Why:* the fan is a small noise source for the microphone, and the Jabra would block the air intake.

> Optional: four rubber feet or M2.5 standoffs under the board's mounting holes keep it off the surface. A proper housing comes in step 2.6.

### 6. Connect the Jabra

1. Plug the Jabra's USB cable into one of the **black USB 2** ports. *Why:* the Jabra is a USB 2 audio device anyway, and this keeps the blue USB 3 ports free.
2. Don't pair it over Bluetooth. We only use USB.

> Why the 27 W supply matters here: the Pi 5 limits the total current for all USB ports to 600 mA unless it detects a 5 A supply, which raises the limit to 1.6 A. The Jabra plus a Pico later need some of that budget.

### 7. (Optional) Ethernet

Plug a network cable from the Pi to your router. For the first boot this avoids Wi-Fi problems. You can switch to Wi-Fi-only later.

### 8. First power-on (no microSD)

This just checks that the board, power supply and cooler are fine.

1. Plug the power supply into the wall socket.
2. Plug the USB-C cable into the Pi **last**. The Pi 5 starts automatically; you don't need to press the button.
3. Watch for 10–20 seconds:

| What | Expected | Meaning |
|---|---|---|
| Red LED | On | Power is good |
| Green LED | Blinks in a repeating pattern ⚠️ | No boot media found — correct, the microSD isn't in yet |
| Fan | May spin briefly or not at all ⚠️ | Both fine: the fan is temperature-controlled |
| Jabra | LEDs light up ⚠️ | It gets USB power |
| Monitor (if connected) | Bootloader diagnostic screen | Pi is alive and looking for an OS |

4. Unplug the USB-C cable. No OS is running, so this is safe right now.

### 9. Learn the shutdown rule (for every time after this)

From step 1.3 on, an OS runs on the microSD. Pulling the power while it's writing can corrupt the card.

- Always shut down first: `sudo poweroff` over SSH, or a short press on the power button ⚠️.
- Wait until the green LED stops blinking, then unplug.

## Verify

- [ ] Cooler sits flat and doesn't wiggle.
- [ ] Fan cable is in the FAN connector, clear of the blades.
- [ ] Jabra is in a black USB 2 port.
- [ ] Red LED turns on when power is connected.
- [ ] Nothing metal near the GPIO header.

**Done when:** the Pi powers on (red LED on, green LED shows activity). ✅

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| No LED at all | Power not reaching the Pi | Check the wall socket, re-seat the USB-C plug, make sure you're using the 27 W supply |
| Cooler wobbles or one side lifts | A push pin isn't fully clicked in | Support the board from below and press that pin again |
| Fan cable won't go in | Wrong orientation or cover still on | Check the cover is removed, flip the plug, don't force |
| Fan never spins | Normal while the Pi is cool or idle | Check again in 1.3 under load; the fan is temperature-controlled |
| Jabra shows no lights | Port or cable issue | Try the other black USB port |

## Notes for later phases

- **2.6 Body design:** the cooler is a blower — the housing needs an air intake above the fan and a vent in front of the side exhaust. Measure the Pi + cooler stack height with calipers before modelling. Use PETG HF for parts close to the Pi.
- **2.2 Motion controller:** the GPIO header stays free. The Pico connects over USB (black USB 2 port). Servos get their own power supply with a shared ground — never from the Pi (golden rule 1). The 1.6 A USB budget is for data devices, not motors.
- **4.2 Camera:** uses one of the flat-cable CAM/DISP connectors, not USB.
- The FAN connector is taken by the cooler. No extra fans from the Pi.
