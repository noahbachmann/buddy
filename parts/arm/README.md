# Arm (v0, simple)

Two-joint arm at average adult size: shoulder pitch and elbow flexion, both driven by standard-size servos. The forearm ends in a flat wrist plate, so the phase 3 hand can attach to it.

| | Human (avg. adult) | Model |
|---|---|---|
| Shoulder → elbow | 300–330 mm | 300 mm |
| Elbow → wrist | 250–270 mm | 260 mm |
| Upper arm Ø (front/back × side) | ~90–100 mm | 90 × 82 mm |
| Elbow width | ~65–75 mm | 73 mm |
| Wrist (front/back × side) | ~55–60 × 40 mm | 60 × 42 mm |

Range (checked for collisions in `assembly.scad`, servo included): elbow ±115°, shoulder ±100°.

## Files

Parametric OpenSCAD. All dimensions are in `params.scad`. `./build.sh` renders `stl/` (OpenSCAD 2021.01+).

| File | What | Print |
|---|---|---|
| `params.scad` | All dimensions: human sizes, servo, bearings, clearances | |
| `lib.scad` | Shared geometry: knuckle (servo housing), fork, tube, couplers | |
| `assembly.scad` | Posed preview (`shoulder`, `elbow` angles), with servo stand-ins | – |
| `shoulder-mount.scad` | Shoulder servo housing + plate (4× M4) that bolts under a shelf | plate down |
| `upper-arm-top.scad` | Fork around the shoulder | fork up |
| `upper-arm-bottom.scad` | Elbow servo housing | knuckle up |
| `forearm-top.scad` | Fork around the elbow | fork up |
| `forearm-bottom.scad` | Wrist plate (Ø20 cable/tendon hole, 4× M3 on 32 × 20) | plate down |
| `couplers.scad` | 2 rings that join the halves | standing |

The STLs are already in print orientation. Each limb is cut in two because 300 mm doesn't fit the P2S (256 mm).

## How a joint works

```
         idler cheek        knuckle (servo inside)        horn cheek
   M8 bolt ─► │ 608 │◄─ boss ─┤  nut │ servo body ├ flange │ horn ├─ 4× M3
```

- The **knuckle** holds the servo, with the shaft on the pivot axis. The servo slides in from the side and its flange is screwed onto the knuckle face.
- The **fork** of the next limb grips the knuckle on both sides. One cheek is screwed to the servo's disc horn. The other cheek carries a 608 bearing on an M8 bolt.
- **Why the bearing:** the bearing carries the load on the second side. Without it, the arm's whole weight would bend the servo's output shaft sideways. Cheap servos fail exactly that way.
- The M8 bolt only clamps the bearing's **inner race** to the knuckle (via the printed boss). The outer race turns with the fork. Tightening the bolt therefore doesn't brake the joint.

## Parts

| Qty | Part | Note |
|---|---|---|
| 2 | Standard-size servo, 25T spline | Shoulder ≥ 35 kg·cm (e.g. DS3235), elbow ≥ 20 kg·cm (DS3218/DS3235); see torque below |
| 2 | 25T aluminium disc horn, 4× M3 threaded | Usually included with the servo |
| 2 | 608 bearing (608-2RS) | Skate bearing, 8 × 22 × 7 |
| 2 | M8×20 hex bolt + M8 nut | DIN 933 / DIN 934 |
| 4 | M8 washer (DIN 125) | 2 under each bolt head |
| 8 | M3×6 socket head | Fork cheek → horn. ⚠️ Must not poke through the horn |
| 8 | M3×10 screw | Servo flanges, self-tapping into 2.5 mm pilot holes |
| 4 | M4 screw | Shoulder mount plate |
| 1 | Servo extension cable ~50 cm | Elbow servo out of the upper arm |
| – | 2-part epoxy or CA glue | Couplers |

## Print (Bambu P2S)

- PETG (project rule: PETG for every part). 0.2 mm layers, 4 walls, 15 % gyroid infill.
- **No supports.** Roofs inside the tubes are 45°, and horizontal holes are teardrops pointing up. Print in the exported orientation.
- Print `upper-arm-top` first: it contains the fork and a bearing seat, so check the fit before printing the rest. If the 608 is too tight or too loose, adjust `press` in `params.scad` (default 0.1 mm), then run `./build.sh`.

## Assembly

1. **Bearings:** press a 608 into each idler cheek from the **inside** (`upper-arm-top`, `forearm-top`). Use a vise with a flat block, and push only on the outer race. It stops against the outer lip.
2. **Couplers:** glue each limb's two halves using its ring. Dry-fit first. The ellipse also fits rotated 180°: the servo opening in the knuckle and the horn cheek must be on the **same side**, and the cable hole goes to the back.
3. **Nuts:** push an M8 nut into the hex slot of each knuckle (shoulder mount, elbow). The slot opens to the front.
4. **Centre the servos** before fitting the horns: 1500 µs pulse, using a servo tester (~CHF 5) or the Pico from step 2.2 (`LIMIT`, then `MOVE <ch> 1500 0`, guide step 9). Otherwise the range won't be symmetric.
5. **Servo in:** feed the cable into the channel first (it leads up into the stem or tube), then lay the servo into the pocket from the open side and fit 4× M3×10 through the flange. Then fit the horn and its centre screw, with the limb pointing straight down.
6. **Fork on:** slide the next limb on from below. The boss on the knuckle slides along the groove in the idler cheek into the bearing. Screw the horn cheek to the horn (4× M3×6). Push the M8 bolt with 2 washers through the bearing into the nut and tighten it firmly.
7. **Elbow cable:** route it up the upper arm and out of the hole at the back near the shoulder.

## Torque ⚠️ estimate

Measured from the models, upper bound (solid print; infill makes it lighter). Arm stretched out horizontally:

| Joint | Without hand | With hand (~0.5 kg at the wrist, InMoov-like) |
|---|---|---|
| Elbow | ~3 kg·cm | ~20 kg·cm |
| Shoulder | ~24 kg·cm | ~55 kg·cm |

- A 35 kg·cm servo at the shoulder covers the arm alone, with margin.
- With a hand, a standard-size servo can't hold the arm out horizontally. Options for later: a bigger servo (e.g. DS5160, which isn't standard size, so it needs a new knuckle), a gear reduction, or a spring/counterweight.
- Stall current of a 35 kg·cm servo is ~3 A at 6–7 V. **Never power servos from the Pi** (golden rule 1): use a separate supply with a shared ground (step 2.2).

## ⚠️ Untested assumptions

- Servo dimensions in `params.scad` are typical for DS3218/DS3235/MG996R-class servos, not measured. Check `servo_horn_face` (flange underside → outer face of the mounted horn) and `horn_pcd` especially, since the fork's horn cheek depends on them.
- The cable is assumed to leave the servo at the end away from the shaft. If yours exits at the shaft end, a side groove in the pocket lets the cable run back along the body.
- The 608 press fit and M8 nut slot fit depend on the printer. Check them on the first print.

## Later

- **3.1 Hand:** the wrist plate is a placeholder. The InMoov hand puts its finger servos in the forearm, and the forearm is hollow (~75 × 65 mm inside near the elbow) for that.
- **2.3 Motion service:** clamp angles in software (elbow ±115°, shoulder ±100° minus margin). The servos can drive the parts into each other beyond that.
- **2.6 Body:** the shoulder mount needs a flat underside to bolt to (90 × 80 mm plate, 4× M4 on 70 × 56).
