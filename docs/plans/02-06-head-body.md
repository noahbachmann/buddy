# 2.6 – Head & body design: plan

> A plan, not a guide: decisions, constraints and the order of work. The CAD itself comes from
> `docs/plans/02-06-cad-prompt.md`; `docs/guides/02-06-body-design.md` replaces this file once Buddy is built.
> ⚠️ = untested assumption.

## Goal

A printed head and body that hold all of Buddy: the 2.5 neck module, Pi 5, the Pico board, the Jabra and the power
entry, with real cable management and service access. Then speech-to-text is retrained, because the mic sits
somewhere new.

**Done when:**

- Everything is inside. Only two cables leave the body: the Pi's 27 W USB-C and the servo supply's 5 V barrel plug.
- Closed body, 10 min `stress-ng` on all cores: `vcgencmd get_throttled` = `0x0`, and the temperature no more than
  5 °C ⚠️ above the same test with the Pi lying open.
- Bluetooth to the Nests no weaker than now (bathroom Nest Mini −69 dBm at the Pi, `bluetoothctl info`).
- STT on a new test session (Jabra in its final place) at least as good as the current numbers after retraining
  (`docs/voice-training.md`).
- microSD, the Pico's BOOTSEL button and the servo power switch are reachable without unscrewing the chassis.
- With the head moving, no rattle you can hear in a recorded clip.

## Prerequisites

- 2.5 done: the neck module works, its bolt pattern and cable exit are fixed.
- 2.1 tolerances known (insert holes, press fits, sliding fits).

## Concept (not a design)

```
        ┌───────────┐  head: tilt servo, eyes (2.4), camera (4.2), ballast pocket
        │  ◯     ◯  │  nothing hot, nothing heavy, removable face plate
        └─────┬─────┘
          neck module (2.5): pan bearing + servo, cable loop, plug at its base
   ┌──────────┴───────────┐
   │  ╭───╮               │  upper body: Jabra in a foam-lined cradle (decoupled)
   │  │ J │  neck mount   │
   │  ╰───╯               │
   │  Pi 5 │ Pico board   │  lower body: chassis sled, air in at the bottom/side, out at the back top
   └──────────────────────┘  rubber feet; back panel: USB-C (Pi), 5 V barrel + switch (servos)
```

### Inner chassis + outer shells

All electronics and the neck module mount on an **inner chassis** (one stiff PETG frame). The shells only cover it.
Why: shells can be redesigned for looks without touching anything that carries load or holds a cable, and
everything can be tested on the open chassis first. All screws go into M3 heat-set inserts.

### Jabra position: the biggest decision (it changes what the mic hears)

| Option | Good | Bad |
|---|---|---|
| A: flat on the body's top deck in front of the neck, grille up | Closest to today's desk use: smallest STT change | Large footprint (≥ 130 mm in front of the neck); the head looks over a puck |
| **B: upright in the body front, grille toward Dev ("chest")** | Mic and speaker face Dev; compact (body ≈ 140 mm wide); reads as a robot | Biggest acoustic change; needs a decoupled cradle |
| C: under the body or closed in | Hidden | Muffled mic and speaker: no |

**Recommendation: decide by measurement, not by looks.** Build both A and B as a cardboard mock-up, record the `test`
set in each (`deploy/record.sh <label> test`, about 10 min each), score with the installed model (`deploy/train.sh
eval`). Better score wins. B is the favourite if the scores are close.

Rules for either position:
- The grille stays completely free: no shell in front of it, no cavity behind it that booms.
- The Jabra sits in a **cradle lined with foam or rubber** (neoprene or silicone pads, cut to size), not hard against
  PETG that the neck module is screwed to: servo noise would travel through the frame straight into the mic. The
  neck module sits on rubber grommets for the same reason.
- Its USB cable is fixed to the puck: coil the extra length on a printed spool or in its own base groove ⚠️ (measure the
  length).
- The touch buttons (mute, volume) stay reachable, or get covered on purpose: an accidental mute makes Buddy deaf.

### Pi 5

- Lower body, on the chassis with M2.5 standoffs (58 × 49 mm hole pattern).
- Envelope with the Active Cooler: 85 × 56 mm board, ≈ 20 mm tall ⚠️ (the USB/Ethernet stack is the highest part, the
  cooler 13.7 mm). Plus the room the plugged-in cables need to bend: measure with the real cables.
- **Air:** the Active Cooler is a blower: it pulls air in from above and pushes it out sideways through the fins.
  Intake above the blower, a vent right at its side exhaust, ≥ 5 mm air gap around the heatsink. Keep the Jabra mic
  away from the exhaust (fan noise). PETG softens around 80 °C ⚠️, the heatsink gets ~60–70 °C under load ⚠️.
- **Power:** the 27 W cable plugs straight into the Pi through a slot in the back panel, with strain relief. No
  panel-mount USB-C extension: a weak one breaks the 5 A negotiation and the Pi then limits all USB ports to 600 mA.
- **microSD** points to a service hatch (recovery without disassembly).
- **Antenna:** no screws, servo cables or other metal near the Wi-Fi/Bluetooth antenna end ⚠️; place it high. This is
  also the cheap lever for the bathroom Bluetooth crackles (Phase 1 → Deferred).
- Ethernet stays unused (Wi-Fi), so that end may face inside.

### Pico board

- On a **removable sled with the breadboard's footprint** (82 × 55 mm ⚠️ measure). 2.6 replaces the breadboard with a
  soldered perfboard of the same size (2.2 → Notes for later phases): Dev's first soldering, a guide section of its
  own. The sled fits both.
- Short USB cable to the Pi (right-angle plugs save room).
- **Servo supply:** the DC socket is panel-mounted in the back, plus a **servo power switch** next to it: a physical
  "motors off" while working on the head. Servo 5 V never touches the Pi (golden rule 1); shared ground only via the
  Pico board.
- **Room for Phase 3:** five hand servo cables come in from the arm side later; leave a channel and spare space on the
  sled.

### Cable management

- Every cable has a defined path: channels in the chassis, zip-tie anchors, clips. No loose cable may reach the pan
  loop or the fan.
- Strain relief where cables leave the body and at the neck plug.
- Neck cables end in a connector on the chassis (servo extension now, eyes later, camera FPC later).
- Keep power (servo supply) and signal (USB, FPC) apart where possible.

### Reserve now for later phases

| Phase | What | Reserve |
|---|---|---|
| 2.4 | Eyes (2 × GC9A01 round, ~Ø 33 mm visible ⚠️) | Face plate with two openings, removable; cable path through the neck |
| 4.2 | Camera Module 3 (25 × 24 mm ⚠️) | Spot between the eyes; FPC path to the Pi's CSI connector (the Pi's CSI side faces the neck exit) |
| 3 / 4 | Arm with hand: shoulder plate 90 × 80 mm, 4 × M4 on 70 × 56 (`parts/arm`) | A side face of the chassis built to take it, M4 inserts |

⚠️ **Scale mismatch:** the arm in `parts/arm` is human-sized (300 + 260 mm, up to ~55 kg·cm at the shoulder with a
hand). A desk-sized body won't stand up to that: it tips over. Either the body gets a heavy/clamped base, the arm
mounts to the desk/shelf instead (its plate already "bolts under a shelf"), or the arm shrinks. Decide in Phase 4; for
2.6 only don't block the mounting face.

## Materials

| Part | Material | Why |
|---|---|---|
| Chassis, neck module, sled, brackets, body shell around the Pi | **PETG** | Carries load, has fits, or sits near the Pi's heat (PLA softens at ~55 °C ⚠️) |
| Support interface under PETG overhangs (bearing seats, servo pockets) | **PLA** on the second nozzle | PLA doesn't bond to PETG: supports snap off and leave clean surfaces. Supports become an option, still not the default |
| Head shell, face plate, eye rims, accents (nothing hot inside) | PETG, or **PLA** for colour and finish | PLA prints crisper and comes in more colours; fine as long as it's away from the Pi and out of the sun |
| Jigs, test stand, mock-ups | PLA | Cheap and fast; not for fit tests (fits differ between PLA and PETG) |
| Damping: Jabra cradle lining, neck mount, feet, cable grommets | **Bought:** neoprene/silicone pads, rubber grommets, self-adhesive rubber feet | No TPU |

- Printer: Bambu Lab X2D + AMS: two nozzles, so a PETG part with a PLA support interface or a two-colour face prints
  in one go. Build volume 256 × 256 × 260 mm (235.5 × 256 with both nozzles in use).
- Print rules as in the arm: 0.2 mm layers, 4 walls, 15 % gyroid, no supports where the geometry allows (45° roofs,
  teardrop holes); parts split with keyed joints when bigger than the bed.

## Order of work

| # | Who | What |
|---|---|---|
| 1 | Dev | Measure the real parts with cables plugged in (list in the CAD prompt); photos |
| 2 | Dev | Cardboard mock-up, Jabra A vs B: record a `test` session each; Claude scores them |
| 3 | Dev | Decisions below; fill in the CAD prompt (style, size, colours) |
| 4 | Claude | CAD stage 2: chassis first (function), then shells (looks) |
| 5 | Dev | Print the chassis, mount everything open: thermal test open, cable routing, Bluetooth RSSI |
| 6 | Dev | Perfboard (soldering guide section) |
| 7 | Dev | Shells on: closed thermal test, rattle check, RSSI |
| 8 | Dev + Claude | Voice: extend the `train` corpus (Phase 1 → Deferred "Next voice session"), record in the final mic position, retrain, eval |
| 9 | Claude | Guide `docs/guides/02-06-body-design.md`; `buddy-hardware.md`; progress table |

## Decisions for Dev

| # | Question | Recommendation |
|---|---|---|
| 1 | Jabra position | Measure A vs B on cardboard, then decide (B if close) |
| 2 | PLA for the head shell and face | PETG unless you want a colour or finish only PLA has |
| 3 | Look: size, shape, colours | Dev's call, in the CAD prompt |
| 4 | CAD tool | Parametric OpenSCAD in `parts/` like the arm (Claude can build and check it), Onshape only for the look |
