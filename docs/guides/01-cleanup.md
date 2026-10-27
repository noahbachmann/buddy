# 1 – Cleanup: your part of finishing Phase 1 (temporary)

> Written 2026-10-25 at the Phase 1 review. Everything here needs you: your voice, the music box, the Spotify login.
> The code and the other docs are done and describe the state *after* this guide. **Delete this file when you're
> through** (and tell Claude the results that need a decision: steps 2, 8, 10). Items marked ⚠️ are untested assumptions.

## Goal

Buddy understands you reliably with the music box playing, and the speech-to-text model is trained on enough of the
right data. Targets, measured with `deploy/train.sh eval` on a test session you record in step 6:

| | Now (first fine-tune, old test set) | Target |
|---|---|---|
| Intent right (what Buddy does) | 28 / 43 = 65 % | ≥ 95 % |
| Word error rate | 11.7 % | < 5 % |
| Speech-to-text time on the Pi | 1.75 s | ≤ 0.3 s |

**Done when:** with the box at normal volume, "Hey Jarvis" triggers from 1 m and 2–3 m, the box ducks, and the command
is right, 9 of 10 times each.

## Why another recording round (the review, short)

- **Data:** 134 training clips in 2 sessions, one with the box. The test set (57 clips) is ~10 dB quieter than the
  training clips and doesn't say whether the box was playing. No wake-word clips. Not enough to reach the targets, and
  not varied enough to know where the remaining errors come from.
- **Broken clips:** the old recorder numbered by file count. After you deleted clip 62, the second run overwrote clip 68,
  so one label was wrong. The importer repairs that; the new recorder numbers by the highest clip.
- **Validation leaked:** the old notebook validated on sentences it had trained on, so its "best epoch" was optimistic.
  The new training holds out whole sentences.
- **brain** missed 15 of 43 music phrasings even with perfect text ("put on …", "a bit louder", "skip this one", "set the
  volume to 50 %", …). Fixed, and checked against the corpus by `deploy/test.sh`.
- **Speed:** your current model with `BUDDY_STT_AUDIO_CTX=auto` is 9× faster (0.2 s) at a small accuracy cost, without
  even being trained for it. The new training trains for it.

## Prerequisites

- Repo pulled on the PC; the Pi runs the current code (`deploy/deploy.sh`, last done 2026-10-27: new test system,
  ears endpointer and spotify refactored without behaviour changes; step 10 is their first live test).
- On the PC: uv, NVIDIA driver, `ssh buddy` (`docs/voice-training.md` → One-time PC setup).
- The music box on, in the same network, logged in to your Spotify account.

## Steps

Commands run **on the PC** in the repo's `skeleton/` folder unless marked **Pi** (= in `ssh buddy`).

### 1. Spotify: new login, the box's name

Liked songs need a new permission. **Pi:**

```bash
while IFS="=" read -r k v; do [[ "$k" =~ ^BUDDY_ ]] && export "$k=${v%$'\r'}"; done < ~/buddy/config/buddy.env
~/buddy/bin/buddy-spotify --login
~/buddy/bin/buddy-spotify --cmd devices
```

`--login` works like in 1.9 (open the URL on the PC, paste the failing `127.0.0.1` URL back). Start a song on the box
from your phone, then `--cmd devices` must list it, e.g. `"Main speaker" (Speaker, active, volume 40 %)`. The name in
quotes must be what `BUDDY_SPOTIFY_DEVICE` says in `~/buddy/config/buddy.env` (case doesn't matter).

### 2. The box from standby ⚠️

Pause the music and leave the box alone until it goes to standby (often 10–20 min). **Pi:**

```bash
~/buddy/bin/buddy-spotify --cmd devices     # still listed?
```

Then say "Hey Jarvis, play Spotify". **Tell Claude** whether the box was listed in standby and whether it started.
Many Connect speakers drop out of the list in standby; spotify then tries the last id it saw (only works if the service
saw the box since it started). If it doesn't start, the fix depends on the box (model name helps).

### 3. Ducking

With music playing on the box at your normal volume. **Pi:**

```bash
~/buddy/bin/buddy-spotify --cmd duck        # ducked: clearly quieter
~/buddy/bin/buddy-spotify --cmd restore     # back to before
```

Then live: "Hey Jarvis, …" (say nothing): the music drops right after the wake word and comes back ~1.5 s after Buddy
stops listening. "Hey Jarvis, louder" while it's down: it comes back *louder than before*. If `duck` says
`refuses remote volume`, the box doesn't allow it: tell Claude.

Tune in `buddy.env` if needed, then `systemctl --user restart buddy@spotify`: `BUDDY_SPOTIFY_DUCK_PERCENT` (30; lower =
quieter while you speak), `BUDDY_SPOTIFY_DUCK_HOLD_MS` (1500).

### 4. Your names in the corpus

```bash
micro tools/voice/commands.tsv
```

Replace the example artists, songs and playlists with what you really ask for, especially names whisper gets wrong.
The "other speakers" section has yours (desktop-1, Bathroom speaker, Pixel 8, my-personal-tv): check them against
`buddy-spotify --cmd devices` with each one awake (Spotify currently lists your TV as just "TV"), and fix the
sentences if a name differs.
5+ sentences per name, some in `train`, some (rephrased) in `test`. Then check that brain still understands everything (builds and runs the C++ tests on the Pi, which has the whole
toolchain; nothing is restarted):

```bash
deploy/test.sh --pi -R Corpus
```

Expected: `100% tests passed`. A failing sentence shows what brain made of it (`normalized: …`): fix
`services/brain/rules.cpp` or tell Claude.

Commit the corpus (it's your music taste, not your voice: fine in the public repo).

### 5. Bring the old recordings over

The old sessions stay useful after repair. Both are still on the Pi (`~/voice-data.tar.gz`, `~/buddy/voice-test`):

```bash
scp buddy:voice-data.tar.gz /tmp/ && mkdir -p /tmp/old && tar xzf /tmp/voice-data.tar.gz -C /tmp/old
deploy/train.sh import /tmp/old/voice-data 2026-10-24-old-train
rsync -a buddy:buddy/voice-test/ /tmp/old/voice-test/
deploy/train.sh import /tmp/old/voice-test 2026-10-24-old-test --set test
deploy/train.sh check
```

Expected: `repaired: 00068.wav: listed twice, kept "Play Spotify." …` and `00062.wav: file missing …`, then 134 train
and 57 test clips. When `~/buddy-voice` is backed up, clean the Pi: `rm -rf ~/buddy/voice-data ~/buddy/voice-test
~/voice-data.tar.gz ~/cmds.wav ~/hello.wav ~/test-*.wav` (**Pi**).

### 6. Record: 4 training sessions + 1 test session

Different days or at least different hours; one condition per session. ~10–15 min each.

| Session | Command | Condition |
|---|---|---|
| desk | `deploy/record.sh desk` | Your chair, normal voice, no music |
| far | `deploy/record.sh far` | 2–3 m away, a bit louder, no music |
| box | `deploy/record.sh box` | Box at your normal listening volume, **music with vocals**, from your chair |
| box-loud | `deploy/record.sh box-loud` | Box louder, a different genre, from 1–2 m |
| test | `deploy/record.sh test-mixed test` | Another day; first half with the box playing, second half without |

- Speak like you talk to Buddy, not like reading. Press Enter *after* the sentence.
- Watch the peak after each clip: −20 to −6 dB is good; `⚠ quiet` → `r` and again louder.
- Each session ends with 20× "Hey Jarvis": vary how you say it.
- Result: ~4 × 110 + 134 old ≈ 570 training clips, ~110 + 57 test clips.

### 7. Train and score

```bash
deploy/train.sh
```

It pulls, checks, trains base.en with the short audio context, converts, then scores on the Pi against the installed
model, per session. Read the intent column first, especially for the box sessions. Then also try tiny.en (faster, maybe
good enough now):

```bash
deploy/train.sh --base tiny.en --no-pull
```

Install the best one at the prompt (or later: `deploy/train.sh install <path to .bin>`). Rule: clearly better intent
than the current model, ≤ 0.3 s.

### 8. Echo cancellation: keep or remove? (decides D13)

Train once on the raw mic and compare each model on its own source:

```bash
deploy/train.sh --source raw --no-pull --no-eval
deploy/train.sh eval <aec model .bin> --source aec --ctx auto
deploy/train.sh eval <raw model .bin> --source raw --ctx auto
```

If raw is at least as good on intent, including the box sessions: echo cancellation only costs (CPU, the USB buffer
workaround, noise suppression damaging speech). **Tell Claude** the two results: it either removes AEC (configs,
`BUDDY_EARS_DEVICE=default`, D8/D13) or keeps it and drops the ⚠️ in D13.

### 9. Wake word threshold with the box

**Pi:** stop the unit, run ears with debug scores:

```bash
systemctl --user stop buddy@ears
while IFS="=" read -r k v; do [[ "$k" =~ ^BUDDY_ ]] && export "$k=${v%$'\r'}"; done < ~/buddy/config/buddy.env
BUDDY_EARS_DEBUG=1 ~/buddy/bin/buddy-ears | grep -E 'score|wake word'
```

With the box at normal volume: "Hey Jarvis" 10× from 1 m, 10× from 2–3 m, then 5 min music alone. Note the real scores
and the highest score from music alone. Threshold in between (`BUDDY_WAKE_THRESHOLD`, default 0.5). Ctrl+C,
`systemctl --user start buddy@ears`.

### 10. The live test ("Done when")

Box at normal volume, from 1 m and from 2–3 m, 10× each: "Hey Jarvis, play \<artist\>", "… next track", "… a bit
louder", "… pause". Count: wake word triggered / box ducked / right action. **Tell Claude** the counts and what failed.

### 11. Keep collecting (optional, recommended)

**Pi**, in `~/buddy/config/buddy.env`: `BUDDY_EARS_SAVE_DIR=/home/dev/buddy/voice-live`, then
`systemctl --user restart buddy@ears`. After a few weeks: `deploy/train.sh pull`, `deploy/train.sh label`, retrain.

## Verify

- [ ] `--cmd devices` lists the box; standby behaviour reported (step 2).
- [ ] Ducking works; "louder" while ducked comes back louder (step 3).
- [ ] Corpus has your names, `deploy/test.sh --pi -R Corpus` passes (step 4).
- [ ] `deploy/train.sh check`: ≥ 500 training clips, ≥ 150 test clips, no warnings.
- [ ] New model installed: intent ≥ 95 %, WER < 5 %, ≤ 0.3 s on the new test session (step 7).
- [ ] AEC decision reported (step 8); wake word threshold set (step 9).
- [ ] Live test passed (step 10). Then delete this file.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `--cmd devices`: `no devices` | Nothing on your account is awake: start a song on the box from the phone first |
| Box listed but `play` fails with 404 | The box went to standby between lookup and play: try again; report (step 2) |
| `duck`: `not ducked (box not active or not playing?)` | The box must be the active device and playing |
| `deploy/record.sh`: prompts but no recording | `ssh buddy pw-record --list-targets`; is `buddy_aec_source` there? (`setup.sh audio`) |
| `deploy/train.sh`: `uv missing` / no CUDA | `docs/voice-training.md` → One-time PC setup |
| Training: `kept epoch 0` | Not enough new data, or the sessions failed `check`: look at its output |
| Anything else | `docs/voice-training.md` → Troubleshooting |
