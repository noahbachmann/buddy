# 1.11 – Music ducking & echo cancellation

> Written 2026-10-23. **Not run on the Pi yet.** I checked the PipeWire config against the source of PipeWire 1.4.2 (what Debian 13 ships). I compiled `buddy-mixer` and the ears changes in the sandbox (syntax check only). Items marked ⚠️ are assumptions.

## Goal

Buddy hears "Hey Jarvis" while it plays music, even though the Jabra's microphone sits a few centimetres from its speaker. While Buddy listens to a command or answers, the music is turned down.

**Done when:** the wake word works over music at normal volume.

## Prerequisites

- 1.6–1.10 work: wake word, speech-to-text, brain, Spotify via Soloist, voice replies.
- Your result from 1.4 step 8 (Jabra echo test). The report "Buddy can't hear me while music plays" says the Jabra's own echo cancellation isn't enough on its own.

## Parts / tools

| Tool | Where | Role |
|---|---|---|
| PipeWire `libpipewire-module-echo-cancel` + WebRTC AEC (`libspa-aec-webrtc`) | Already installed with PipeWire (packages `libpipewire-0.3-modules`, `libspa-0.2-modules`) | Removes Buddy's own sound from the microphone |
| PipeWire `libpipewire-module-loopback` | same | Virtual sink `buddy_music` that Soloist plays into |
| `deploy/pipewire/buddy-audio.conf` | → `~/.config/pipewire/pipewire.conf.d/` | Loads both modules |
| `pactl` (`pulseaudio-utils`) | apt, via `setup-pi.sh` | Sets the music sink's volume |
| `buddy-mixer` | new service `services/mixer` | Turns the music down while Buddy listens or speaks |

## Background

### Why music breaks the wake word

The microphone hears everything in the room, and the loudest thing is the speaker 5 cm away. Your voice from 2 m is quieter than the music at the mic, so openWakeWord's score never reaches the threshold. **Ducking alone can't fix this:** music is only turned down *after* the wake word, and the wake word itself must get through first. Only echo cancellation helps with that.

### Echo cancellation (AEC)

Buddy knows exactly what it plays, so it can subtract it from what the microphone records. That is what video calls do (WebRTC's AEC is the one in Chrome and most call apps).

```
                       reference = what the Jabra plays (music + replies)
                                   │  (monitor of the default sink)
                                   ▼
 Jabra mic ──► buddy_aec_capture ──► [WebRTC AEC] ──► buddy_aec_source ──► ears
   (your voice + music)              mic − (reference as heard by the mic)   (mostly your voice)
```

- **The adaptive filter:** WebRTC learns how the sound changes on its way from the speaker to the mic: the delay, the room echo, the speaker's colouring. It then subtracts its estimate. That takes a few seconds after music starts, and it relearns when the volume or the room changes.
- **Monitor mode:** normally the echo canceller creates its own sink, and every app has to play into it. With `monitor.mode = true` it listens to the *monitor* (a copy) of the default sink, the Jabra, instead. So everything Buddy plays is covered, with no routing changes.
- **Leftovers:** cancellation reduces the echo by roughly 20–40 dB ⚠️, not to zero. Loud music still leaves some residue. Ducking takes care of the command after the wake word.
- **The defaults stay the Jabra.** Only ears reads the cancelled source, and it does so explicitly (`BUDDY_EARS_DEVICE=pipewire:NODE=buddy_aec_source`). `pw-record` and the other tests from 1.4 still get the raw mic.
- **Same USB device:** mic and speaker share one clock, so the two signals never drift apart. That's the easy case for AEC.

### Ducking

```
 ears/state  listening|transcribing ─┐
 voice/state speaking ───────────────┼─► [mixer] ─► pactl set-sink-volume buddy_music 0.1 (−20 dB)
                                     │              … 1.5 s after both are idle again → 1.0
                                     └─► buddy/mixer/state {"ducked":true|false}  (retained)

 Soloist ──► buddy_music (virtual sink, volume = ducking) ──► Jabra ◄── voice (never ducked)
```

- **Why a separate music sink:** turning down the Jabra itself would turn down Buddy's voice too. Changing Spotify's volume through the Web API takes ~0.5 s, counts against the rate limit, and moves the volume slider on your phone. A local sink volume is instant and leaves Spotify's volume alone.
- **Hold time:** after your command is transcribed, it takes a moment until the reply starts (brain → Spotify Web API → voice). During that gap nothing is "busy". `BUDDY_MIXER_HOLD_MS=1500` keeps the music down that long, so it doesn't jump up and down.
- **Soloist → `buddy_music`:** set with `PULSE_SINK=buddy_music` in `buddy-soloist.service`. ⚠️ This assumes Soloist uses libpulse, which reads that variable. Step 3 checks it.
- **ears ignores wake words while Buddy speaks** (`BUDDY_EARS_IGNORE_WHILE_SPEAKING=1`), so a reply can never wake Buddy. You can't interrupt a reply by voice, but replies are a few words long.

### Decisions made in this step

| Topic | Decision | Why |
|---|---|---|
| AEC | PipeWire echo-cancel, WebRTC, monitor mode | Already installed, no code; covers every sound Buddy plays |
| Where ears listens | `pipewire:NODE=buddy_aec_source` (explicit), defaults unchanged | Easy to compare and to switch back; nothing else changes |
| Ducking | New service `mixer`, virtual sink `buddy_music`, volume via `pactl` | One job per service; instant; Spotify's volume untouched |
| Mixer placement | Pi only (controls the local PipeWire) | Unlike ears/brain (D3) it can't move to a LAN host |

## Steps

Commands run in your SSH session on the Pi unless marked **on the PC**.

### 1. Deploy

**On the PC:**

```bash
git pull
deploy/deploy.sh --setup
```

`--setup` installs `pulseaudio-utils` and the PipeWire config, and restarts PipeWire **only if the config changed**. The restart cuts all sound for a moment. Expected, among the usual output:

```
→ PipeWire: echo cancellation, music sink
  installed, PipeWire restarted
…
  buddy@ears       active
  buddy@brain      active
  buddy@spotify    active
  buddy@voice      active
  buddy@mixer      active
```

### 2. Check the new nodes

```bash
wpctl status
```

Expected, among the rest ⚠️ numbers differ:

```
 ├─ Sinks:
 │  *   48. Jabra SPEAK 510 USB Analog Stereo  [vol: 0.60]
 │      71. Buddy music                        [vol: 1.00]
 ├─ Sources:
 │  *   49. Jabra SPEAK 510 USB Mono           [vol: 1.00]
 │      73. Buddy mic (echo-cancelled)         [vol: 1.00]
```

The `*` must still be on the **Jabra** for both. If it moved to a Buddy node, set it back with your Jabra numbers: `wpctl set-default 48` and `wpctl set-default 49`.

### 3. Music plays into `buddy_music`

Start music on Buddy (Spotify app → devices → Buddy, or "Hey Jarvis, play Queen"). Then:

```bash
pactl list sink-inputs | grep -E '^Sink Input|Sink:|application.name|application.process.binary'
pactl list short sinks
```

Soloist's stream must be on the sink with the number that `list short sinks` shows for `buddy_music`. Now test ducking by hand:

```bash
~/buddy/bin/buddy-mixer --duck      # ok, music clearly quieter
~/buddy/bin/buddy-mixer --restore   # ok, back to normal
```

If the music doesn't change, Soloist isn't on `buddy_music`: see Troubleshooting.

### 4. Hear the difference

Keep the music at your normal volume. Record the raw mic and the echo-cancelled mic **at the same time**, and talk for ~5 s from where you usually sit ("Hey Jarvis, pause the music"):

```bash
pw-record --rate 16000 --channels 1 --format s16 ~/test-raw.wav &
pw-record --target buddy_aec_source --rate 16000 --channels 1 --format s16 ~/test-aec.wav &
sleep 8; kill -INT %1 %2
```

Pause the music, then listen to both:

```bash
pw-play ~/test-raw.wav
pw-play ~/test-aec.wav
```

Expected: in `test-aec.wav` the music is much quieter or gone, and your voice is clear. The first second may still contain music while the filter adapts. Copy them to the PC to compare in Audacity (**on the PC:** `scp buddy:test-*.wav .`).

### 5. Switch ears to the echo-cancelled mic

```bash
micro ~/buddy/config/buddy.env
```

Add (or uncomment and change):

```
BUDDY_EARS_DEVICE=pipewire:NODE=buddy_aec_source
```

```bash
systemctl --user restart buddy@ears
journalctl --user-unit buddy@ears -n 5
```

Expected: `[ears] capturing from "pipewire:NODE=buddy_aec_source" at 16000 Hz`.

### 6. Wake word over music

Turn on debug scores for this test (`BUDDY_EARS_DEBUG=1` in `buddy.env`, then `systemctl --user restart buddy@ears`). In a second SSH session:

```bash
journalctl --user-unit buddy@ears -f
```

With music playing at your normal volume, say "Hey Jarvis" ten times each from ~1 m and from 2–3 m. Count the detections. Optional comparison: switch back to `BUDDY_EARS_DEVICE=default` for one round. That round should miss most of the time, which is the problem you reported.

Then set `BUDDY_EARS_DEBUG=0` again (it logs a lot: microSD wear).

### 7. Ducking end to end

In a second SSH session:

```bash
mosquitto_sub -v -t 'buddy/+/state'
```

With music playing, say "Hey Jarvis, play Daft Punk". Expected order ⚠️ timings vary:

```
buddy/ears/state {"state":"listening"}
buddy/mixer/state {"ducked":true}          ← music drops right after the wake word
buddy/ears/state {"state":"transcribing"}
buddy/ears/state {"state":"idle"}
buddy/voice/state {"state":"speaking"}      ← "Playing Daft Punk." over quiet music
buddy/voice/state {"state":"idle"}
buddy/mixer/state {"ducked":false}         ← ~1.5 s later the music is back
```

Also try "Hey Jarvis" and then say nothing: the music comes back about 4 s + 1.5 s later.

### 8. Tune (only if needed)

| What | Where | Effect |
|---|---|---|
| `BUDDY_MIXER_DUCK_DB` | `buddy.env` | −20 default; −30 if commands still get misheard over music |
| `BUDDY_MIXER_HOLD_MS` | `buddy.env` | Raise it if the music jumps up before the reply |
| `BUDDY_WAKE_THRESHOLD` | `buddy.env` | 0.4 if scores over music stay just below 0.5 (watch for false triggers) |
| `webrtc.noise_suppression` | `deploy/pipewire/buddy-audio.conf`, then **on the PC** `deploy/deploy.sh --setup` | `false` if your voice sounds "underwater" in `test-aec.wav` or wake scores dropped *without* music |

After `buddy.env` changes: `systemctl --user restart buddy@ears buddy@mixer`.

### 9. Reboot test

```bash
sudo reboot
```

After ~1 min, without logging in: "Hey Jarvis, play Spotify", then over the music "Hey Jarvis, pause".

Clean up: `rm ~/test-raw.wav ~/test-aec.wav`.

## Verify

- [ ] `wpctl status`: "Buddy music" and "Buddy mic (echo-cancelled)" exist; the defaults (`*`) are still the Jabra.
- [ ] `buddy-mixer --duck` / `--restore` change the music volume.
- [ ] `test-aec.wav` has much less music than `test-raw.wav`.
- [ ] ears logs `capturing from "pipewire:NODE=buddy_aec_source"`.
- [ ] Over music at normal volume, "Hey Jarvis" triggers from 2–3 m on most tries.
- [ ] The music goes down after the wake word and comes back after the reply.
- [ ] Still works after a reboot.

**Done when:** the wake word works over music at normal volume.

**Optional next:** train speech-to-text on your own voice: `guides/01-11-custom-stt.md`.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| No "Buddy mic (echo-cancelled)" / "Buddy music" in `wpctl status` | Config not loaded or a syntax error | `ls ~/.config/pipewire/pipewire.conf.d/`; `journalctl --user -u pipewire -n 50` (errors name the line); `systemctl --user restart pipewire pipewire-pulse wireplumber` |
| pipewire log: `can't load aec/libspa-aec-webrtc` ⚠️ | WebRTC plugin missing | `ls /usr/lib/aarch64-linux-gnu/spa-0.2/aec/`; `sudo apt install libspa-0.2-modules` |
| Default sink/source became a Buddy node | WirePlumber had no saved default | `wpctl set-default <Jabra id>` for sink and source (step 2) |
| ears: `cannot open audio device "pipewire:NODE=…"` | `pipewire-alsa` missing, or a typo in the node name | `ls /usr/share/alsa/alsa.conf.d/ \| grep pipewire`; the name must be exactly `buddy_aec_source` |
| `test-aec.wav` still full of music | The reference isn't the Jabra's output | `pw-link -l \| grep -A3 buddy_aec_reference` must show the Jabra sink's `monitor_*` ports; if not, the default sink is wrong (step 2) |
| `test-aec.wav`: voice choppy or "underwater" | Noise suppression / double-talk suppression | `webrtc.noise_suppression = false` (step 8); keep the music volume moderate |
| `--duck` changes nothing | Soloist not on `buddy_music` (⚠️ `PULSE_SINK` ignored) | Find Soloist's stream number with step 3's command, then `pactl move-sink-input <number> buddy_music`. WirePlumber should remember this per app ⚠️. Tell Claude if it doesn't stick after `systemctl --user restart buddy-soloist`. |
| No music at all after deploy | Soloist was disconnected by the PipeWire restart | `systemctl --user restart buddy-soloist`, reselect Buddy in the Spotify app |
| mixer log: `cannot run pactl` | `pulseaudio-utils` missing | `sudo apt install pulseaudio-utils` (or `deploy.sh --setup` again) |
| mixer log: `duck failed` / `restore failed` | Sink missing (PipeWire restarted, config gone) | Step 2; `pactl list short sinks` |
| Music jumps up before the reply | Reply slower than the hold time | `BUDDY_MIXER_HOLD_MS=2500` |
| Music stays quiet | mixer stopped while ducked | `~/buddy/bin/buddy-mixer --restore`; `systemctl --user status buddy@mixer` (it also restores when it starts) |
| Wake word still misses over loud music | Echo residue louder than you | Lower the music a bit, `BUDDY_WAKE_THRESHOLD=0.4`, or the custom wake word (`guides/01-06-custom-wake-word.md`, trained with music in the background) |
| Commands cut off or missed after the wake word | ears' level gate uses the background *before* the wake word (music) | `BUDDY_VAD_LEVEL_GATE=0.3`, or more ducking |

## Notes for later phases

- **New sound sources** (timers, radio, sound effects): play anything that should be ducked into `buddy_music` (`PULSE_SINK=buddy_music`, or ALSA `pipewire:NODE=buddy_music`). Play Buddy's own sounds to the default sink.
- **1.12 boot:** the echo-cancel node appears together with PipeWire. ears retries every 2 s until it can open it, and the mixer restores the volume at start.
- **Phase 2 motion:** `buddy/mixer/state` and `buddy/voice/state` are retained, so the eyes can show "listening" / "speaking" without asking ears.
- **Phase 2 body:** the AEC filter learns the speaker-to-mic path. Moving the Jabra inside the body changes that path. It adapts by itself, but repeat step 4 after building the body. Keep the mic away from the fan and the servos: AEC can't remove sounds Buddy doesn't play.
