# 1.6 (optional) – Custom "Hey buddy" wake word

> Written 2026-10-23. **Not run yet.** Based on the community notebook `alfiedennen/openwakeword-colab-2026` (MIT, one commit, May 2026) as it stands today. I checked that its ONNX output fits `buddy-ears` by reading the export code, not by running it. Items marked ⚠️ are assumptions.

## Goal

Get a `hey_buddy.onnx` model that triggers on "Hey buddy" from 2–3 m and holds up better than "hey jarvis" against TV and music. The model is trained once in Google Colab. No Python goes into the repo or onto the Pi (D5). `buddy-ears` loads the model with no code change.

## Prerequisites

- 1.6 with "hey jarvis" works (`guides/01-06-wake-word.md` steps 1–4). You need the debug-score workflow and your Jarvis numbers to compare against.
- A Google account. Colab Pro (≈ 10 USD/month, cancel any time) is recommended but not required.

## Parts / tools

| Tool | Role |
|---|---|
| Google Colab, GPU runtime | Generates the training audio and trains the model |
| [`train_wakeword.ipynb`](https://github.com/alfiedennen/openwakeword-colab-2026) | The notebook. Use it instead of openWakeWord's own notebook (see below) |
| Piper TTS (`en_US-libritts_r-medium`, 900+ voices) | Speaks "hey buddy" thousands of times in different voices |
| ACAV100M features (17 GB), FMA music (8 GB), MIT room echoes | Negatives and background noise mixed into the clips |

### Why not openWakeWord's official notebook

`notebooks/automatic_model_training.ipynb` has been broken on current Colab since late 2025 (issue #296, still open): no `piper-phonemize` wheels for Python 3.12+, `torchaudio.set_audio_backend` removed, missing config keys. Nobody has merged a fix upstream. The community notebook patches these problems, replaces `auto_train` with a short PyTorch loop that does the same job, and exports ONNX with the sigmoid included, so scores come out as 0..1.

## Background: what training does

The first two openWakeWord models (mel spectrogram, Google's speech embedding) stay the same. Only the small third model is trained: a 2-layer network that takes 16 × 96 embedding numbers (1.28 s of audio) and returns one score.

```
 "hey buddy" text ─► Piper, ~900 voices ─► 2000 clips ─┐
 similar-sounding phrases ("hey body", …) ─► clips ────┤ + room echo + music/noise at random volume
                                                       ▼
                       embeddings (16×96) ─► train: positives → 1, everything else → 0
 2000 h of real-world audio (ACAV100M: speech, TV, music) ─► negatives ─┘
```

**Why this helps with TV:** every positive clip is mixed with background noise, and the negatives include hours of real speech and broadcast audio. The model learns "hey buddy over noise = yes, any other talking = no". The stock Jarvis model went through the same kind of training, but on its own phrase. You can't adjust it for your room or for confusable words.

**Risk:** "buddy" is a common word, and "Hey buddy" has fewer syllables than "Hey Jarvis". Expect a few more false triggers, and only keep the custom model if it measurably beats Jarvis (step 7).

## Steps

### 1. Open the notebook and save your own copy

1. Open <https://colab.research.google.com/github/alfiedennen/openwakeword-colab-2026/blob/main/train_wakeword.ipynb>
2. **File → Save a copy in Drive.** Your copy keeps working even if the original is changed or deleted. ⚠️ The pip packages and openWakeWord `master` are still unpinned, so a future Colab image can still break it. If that happens, check the repo's issues first.

### 2. Pick the runtime

**Runtime → Change runtime type:**

| Option | Time | Notes |
|---|---|---|
| **L4 GPU + High-RAM** (Colab Pro) | 75–90 min | Recommended. One run uses ≈ 1 % of the monthly Pro compute |
| T4 GPU (free) | ≈ 2.5 h ⚠️ | Keep the tab **in the foreground**. Free sessions get cut off when idle or backgrounded |
| A100 | No faster | Bottleneck is downloads/CPU, not the GPU |

**A disconnect = a new, empty VM.** The cells only skip finished work within the *same* runtime. After a real disconnect, everything (≈ 25 GB of downloads) starts again. ⚠️ Peak disk use is ≈ 35 GB. Colab's disk is larger than that, but check the disk indicator top right.

### 3. Edit the config cell

Find the cell with **★ EDIT THESE TWO LINES ★** (section "10. Build training config"). Change three things:

```python
TARGET_PHRASE = ['hey buddy']
MODEL_NAME    = 'hey_buddy'
```

`MODEL_NAME` becomes the file name `hey_buddy.onnx`. `buddy-ears` uses the file name as the model name, so `buddy/ears/wake` will carry `{"model":"hey_buddy",…}`.

In the `config = {` block below it, fill the negatives list:

```python
    'custom_negative_phrases': ['buddy', 'hey', 'hey body', 'hey baby', 'hey daddy', 'hey bud',
                                'hey muddy', 'study', 'nobody', 'somebody', 'everybody', 'anybody',
                                'hey buddy holly', 'a buddy', 'my buddy'],
```

Why: these sound close to "hey buddy" or contain one half of it. Training them as "no" teaches the model that "buddy" alone or "hey baby" from the TV isn't the wake word. The notebook also generates its own similar-sounding phrases automatically. This list adds to them.

⚠️ Remove `'hey buddy holly'`, `'a buddy'` and `'my buddy'` if you *want* "…my buddy" at the end of a sentence to trigger. They contain the full phrase or nearly all of it.

Leave everything else at the defaults for the first run (`n_samples: 2000`, `max_negative_weight: 1500`, `steps: 20000`). Change them only after you've measured (step 8).

### 4. Run everything

**Runtime → Run all.** Rough timeline (L4):

| Section | What | Time |
|---|---|---|
| 1–4 | Install, patches, **pre-flight check** | 3–5 min. If the pre-flight cell fails, stop and read its ✗ lines |
| 5–9 | Downloads: ACAV 17 GB, FMA 8 GB, convert | 15–20 min |
| 11 | Piper generates the clips | 10–15 min |
| 13 | Augment + compute features | ≈ 10 min |
| 14 | Training, 3 stages | 30–40 min |
| 15 | Ensemble, ONNX export, browser download | 1 min |

### 5. Listen to a few clips (while it trains)

After section 11 finishes, insert a new cell (**+ Code**) right below it and run it while training continues. This checks that Piper pronounces "buddy" the way you do:

```python
import glob, random
from IPython.display import Audio, display
clips = glob.glob('/content/hey_buddy_output/hey_buddy/positive_train/*.wav')
for c in random.sample(clips, 5): display(Audio(c))
```

Expected: five different voices saying "hey buddy". If it sounds like "hey boody" or "hey bud-ee" with odd stress, stop. Add a phonetic spelling as a second target (`TARGET_PHRASE = ['hey buddy', 'hey budy']`) and run again.

Piper only has American/British English voices. A Swiss accent will score lower than the synthetic voices do. That's normal, and it's why the threshold is tuned on the Pi.

### 6. Check the last cell's output and back up the model

Expected at the end of section 15 (numbers vary):

```
  ensemble averaged: mean acc=0.8…, recall=0.6…, fp/hr=0.3…
  ✓ wrote /content/hey_buddy_output/hey_buddy/hey_buddy.onnx (≈ 790 KB)
  input:  onnx::Flatten_0 ['batch', 16, 96]
  output: output ['batch', 1]
  positive test set: mean=0.6…, recall@0.5=0.6…
```

- `input … 16, 96` and `output … 1` must match exactly: that's what `buddy-ears` feeds and reads.
- Validation recall is measured on hard, heavily augmented clips. 0.5–0.7 is normal and doesn't mean it misses 40 % of your real "hey buddy"s.
- `fp/hr` > 1 → expect TV false triggers. Plan a second run (step 8).

The browser downloads `hey_buddy.onnx` (allow the download if Chrome asks). **Back it up** to a place other than your Downloads folder: models aren't in Git (D9), and the file can't be re-downloaded.

### 7. Install on the Pi and compare with Jarvis

From the PC (Downloads folder):

```bash
scp hey_buddy.onnx buddy:buddy/models/
```

**Offline test first.** Record yourself at ~1 m saying "Hey buddy", pause, "play Spotify", 2 s of silence. Then run the new model on the recording (the env var overrides `buddy.env` for this command only):

```bash
ssh buddy
pw-record --rate 16000 --channels 1 --format s16 ~/hey-buddy.wav    # Ctrl+C when done
BUDDY_WAKE_MODEL=hey_buddy.onnx BUDDY_EARS_DEBUG=1 ~/buddy/bin/buddy-ears --wav ~/hey-buddy.wav
```

Expected ⚠️:

```
[ears] listening for "hey_buddy"
[ears] 1.2 s score 0.31
[ears] 1.36 s score 0.88
[ears] 1.44 s wake word (score 0.95)
```

Also check that it does **not** trigger on your old Jarvis recording:

```bash
BUDDY_WAKE_MODEL=hey_buddy.onnx ~/buddy/bin/buddy-ears --wav ~/hey.wav    # no "wake word" line expected
```

**Live tuning.** Repeat steps 4–5 of the main 1.6 guide with `BUDDY_WAKE_MODEL=hey_buddy.onnx` set in the CLion run configuration, then fill in this table. Run the same TV/podcast test against both models:

| | hey_jarvis_v0.1 | hey_buddy |
|---|---|---|
| Hits / 10 at 1 m | | |
| Hits / 10 at 2–3 m | | |
| Highest score from 5 min TV, not the wake word | | |
| False triggers in the evening soak | | |

**Switch over** only if hey_buddy is at least as good on hits and clearly better on TV. Edit `~/buddy/config/buddy.env` on the Pi:

```bash
BUDDY_WAKE_MODEL=hey_buddy.onnx
BUDDY_WAKE_THRESHOLD=0.5
```

```bash
ssh buddy systemctl --user restart buddy@ears
deploy/logs.sh ears    # expect: listening for "hey_buddy"
```

Start at 0.5. If it misses from 2–3 m but TV scores stay below ~0.2, try 0.4. One user reported 0.4 working better for a custom model.

### 8. Second run (if needed)

Change one thing per run, otherwise you can't tell what helped. Same Colab copy, then **Runtime → Run all**:

| Problem | Change | Cost |
|---|---|---|
| Misses you from 2–3 m | `n_samples: 5000`, `augmentation_rounds: 2` | Clip generation ≈ 2.5× longer |
| TV/music false triggers | `max_negative_weight: 3000` | Slightly lower recall |
| Specific false trigger (e.g. a name on TV) | Add that phrase to `custom_negative_phrases` | — |
| Bad pronunciation (step 5) | Second spelling in `TARGET_PHRASE` | — |

Rename each new model (e.g. `hey_buddy_v2.onnx`) before copying it to the Pi, so you can switch back via `BUDDY_WAKE_MODEL`.

## Verify

- [ ] Section 15 printed input `[batch, 16, 96]`, output `[batch, 1]`.
- [ ] `hey_buddy.onnx` backed up outside Downloads.
- [ ] `--wav`: triggers on "Hey buddy", not on the Jarvis recording.
- [ ] Comparison table filled; decision noted (keep Jarvis or switch).
- [ ] If switched: ≥ 9/10 from 2–3 m, soak shows few false triggers, survives `sudo reboot`.

**Done when:** the step 7 table shows hey_buddy at least as reliable as Jarvis, with fewer TV false triggers, *or* you've decided to keep Jarvis based on the numbers.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Pre-flight cell raises `Pre-flight failed` | Colab image changed (new Python/torch) | Read the ✗ lines; check the notebook repo's issues; don't continue to the downloads |
| Runtime disconnected mid-run | Idle/background timeout (free tier), or compute limit | Keep the tab in front; use Pro; restart with Run all (downloads start over) |
| ACAV or FMA download stalls / `ConnectionError` | Hugging Face or SWITCH server slow | Re-run that cell: it resumes partial files |
| `generate_clips left positive_train with only …` | Piper crashed (often GPU memory) | Re-run the cell; it continues from existing clips |
| No browser download at the end | Popup/download blocked | Files pane (folder icon, left) → `hey_buddy_output/hey_buddy/hey_buddy.onnx` → ⋮ → Download |
| `buddy-ears`: `cannot load model …/hey_buddy.onnx` | Wrong folder or name | `ssh buddy ls ~/buddy/models` |
| Scores stuck near 0 for your voice, but the synthetic clips sounded right | Accent/pronunciation far from Piper's voices | Try spelling variants (step 8); threshold 0.4; otherwise keep Jarvis |
| Triggers when you say "buddy" mid-sentence | Expected for a common word | Add those sentences to `custom_negative_phrases`; raise threshold or `BUDDY_WAKE_PATIENCE=3` |

## Notes for later phases

- **1.11 music:** if Spotify through the Jabra triggers hey_buddy, retrain with `max_negative_weight: 3000` before reaching for echo-cancel. FMA in the training data is music.
- **1.12 / fresh SD card:** `fetch-models.sh` can't download `hey_buddy.onnx` because it has no public URL. If you switch to it, decide where it lives (e.g. a Gitea release asset + one `fetch` line with SHA-256 in `fetch-models.sh`) and record it as a D9 update.
- **2.4 eyes, 2.5 head:** they react to `buddy/ears/wake` regardless of `model`. Don't filter on the model name.
- Each extra wake word is a separate model (a separate run). `buddy-ears` loads one model today.
