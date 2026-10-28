# Voice training: speech-to-text on your voice

How Buddy's whisper model gets trained on your voice, your room and your names, and how to redo it. Everything runs
from the PC: recording happens on the Pi through ssh, training on the PC's NVIDIA GPU, scoring on the Pi with the real
whisper.cpp build. Phase 1 history and numbers: `guides/01-setup-summary.md` → 1.7.

```
 PC                                          Pi
 deploy/record.sh <label> [test] ──ssh──►    records the raw Jabra mic → ~/buddy/voice/<date>-<label>/raw/
 deploy/train.sh                              
   pull ◄──────────────────────────rsync──   sessions (+ saved live commands: train.sh history)
   check: levels, missing files, VAD trim
   train: fine-tune on the GPU, convert to GGML (+ q8_0)
   eval ───────────────────────────ssh───►   buddy-ears --transcribe, buddy-brain --test: scores per session
   install? ───────────────────────scp───►   ~/buddy/models, BUDDY_STT_MODEL + BUDDY_STT_AUDIO_CTX, restart ears
 ~/buddy-voice: the dataset (master copy, never in Git: D16)
```

## Why it helps, and what it can't do

- whisper was trained on 680 000 hours of other people. A few hundred clips of *you* saying *Buddy's* sentences,
  recorded through the Jabra in your room, move it toward exactly that sound. The first fine-tune
  (134 clips) cut the word error rate from 27.5 % to 11.7 %.
- **Short audio context:** models trained here run with `BUDDY_STT_AUDIO_CTX=auto` (encoder window sized to the
  command): ~0.2 s instead of ~1.75 s per command on the Pi. Stock models break with that setting.
- It can't fix a bad recording: too quiet, music louder than you, the key click in the clip. `check` and the recorder's
  level warnings catch those.
- It forgets if overtrained on too little: the training keeps the epoch with the best score on sentences it never saw,
  and the Pi eval on a separate test session decides. Keep a new model only if it wins.

## One-time PC setup

| What | How |
|---|---|
| uv | <https://docs.astral.sh/uv/getting-started/installation/> |
| NVIDIA driver ≥ 570 | PyTorch's CUDA 12.8 wheels (any NVIDIA GPU of the last ~10 years). On WSL2: the Windows driver, nothing inside WSL |
| cmake + C++ compiler (optional) | Builds whisper.cpp's quantize tool once for the `q8_0` model; skipped without |
| `ssh buddy` works | Same alias as `deploy/deploy.sh` (`BUDDY_HOST` to override) |
| Dataset folder | `~/buddy-voice` (`BUDDY_VOICE_DIR` to override). **Back it up**: it's the only part you can't regenerate |

The first `deploy/train.sh` installs the Python packages (PyTorch ~2.5 GB) with uv. Check the GPU:
`uv run --project skeleton --package buddy-voice --extra cuda python -c "import torch; print(torch.cuda.get_device_name())"`.

## The corpus: `skeleton/tools/voice/commands.tsv`

One list for everything: recording prompts, brain's regression test (`deploy/test.sh -R Corpus`), and the intent score. Columns:
`set` (`train` / `test` / `wake`), `sentence` (written as it should be transcribed, digits for numbers), `intent`, `query`.

- Put your real artists, songs and playlists in; 5+ sentences per name, spread over train and test.
- `test` sentences are rephrasings that are never recorded for training: the score on them is the honest one.
- A new phrasing → add it, run `deploy/test.sh -R Corpus`. If brain doesn't understand it, fix `services/brain/rules.cpp`.
- Lights etc. stay in with intent `unknown`: whisper learns the words before a skill exists.

## Record: `deploy/record.sh <label> [train|test] [wake count]`

The label says what's special about the session: `desk`, `far`, `box`, `box-loud`, `tired`. Sentences come in random
order, then 20× "Hey Jarvis" (third argument: how many, 0 = none). Enter = start, speak, Enter = stop **after** you
finished; `r` redoes the last clip, `s` skips, `q` quits. The same label on the same day continues where you stopped.

- After each clip: `peak … dB`. Aim for −20 to −6 dB. `⚠ quiet` → speak up or move closer, then `r`.
- Vary what ears will hear: distance (desk, 2–3 m), loudness, the **music box playing** at normal and loud volume, music
  with vocals (hardest), time of day. One condition per session, so the eval can show each one separately.
- Only the raw mic is recorded (what ears hears, D13). Sessions up to 2026-10-28 also have an `aec/` track (`--source aec`).
- Test sessions (`deploy/record.sh <label> test`) on another day, half of them with the box playing.

## Train: `deploy/train.sh`

Runs pull → check → train → convert → eval → install? in one go (~5–15 min on a desktop GPU for base.en). Options
(`deploy/train.sh --help`): `--base tiny.en|base.en|small.en`, `--source raw|aec|both` (default raw; aec = old sessions only), `--ctx auto|full`, `--epochs`,
`--lr`, `--batch`, `--sessions …`, `--no-eval`, `--force`.

What it does, so the output makes sense:

1. **check**: per session clips, median peak/RMS, seconds of speech; warns about missing files, clips without speech,
   too quiet, clipped. Stops on warnings unless `--force` (fix with `r` in the recorder or delete the row).
2. **trim**: Silero VAD (the same model as ears) cuts each clip to 0.1 s before and 0.3 s after the speech. That drops
   the Enter-key clicks and matches what ears hands to whisper. Cached in `~/buddy-voice/trimmed/`.
3. **train**: validation = ~15 % of the *sentences* (never their clips in training, whatever the session). SpecAugment,
   learning rate 1e-5, early stopping after 3 epochs without improvement, keeps the best epoch. Prints validation WER
   before/after and the remaining mistakes.
4. **convert**: whisper.cpp's converter from the same pinned release as the C++ build → `ggml-buddy-<base>-<time>.bin`
   (+ `-q8_0.bin`, smaller, sometimes faster) in `~/buddy-voice/runs/<time>-<base>/` with `report.json`.
5. **eval** on the Pi (below) against what's installed, then asks which model to install.

## Eval: `deploy/train.sh eval <model>… [--ctx 0,auto] [--fallback 0,1] [--source raw|aec] [--prompt …] [-v]`

Models: names in the Pi's `~/buddy/models`, local `.bin` paths (copied over first), or `current` (exactly what
`buddy.env` says). Comma lists make a matrix. Clips: every `test` clip, trimmed, copied to the Pi's `/tmp`.

```
variant                                   exact     intent     WER   ms avg/p90
ggml-buddy-base.en.bin ctx=0             40 / 57    28 / 43   11.7%   1751/1799
ggml-buddy-base.en.bin ctx=auto          35 / 57    26 / 43   14.3%    202/257
```

- **intent** is the number that matters: brain gets the right action *and* query (over the clips brain can understand).
  exact and WER explain why. `-v` lists the wrong ones. With several sessions, each gets its own row.
- Install only what clearly wins on intent, especially in the box sessions, without getting slower than ~0.3 s.
- Reports: `~/buddy-voice/evals/*.json`.

## Install: `deploy/train.sh install <model> [--ctx 0|auto]`

Copies the model to the Pi, sets `BUDDY_STT_MODEL` and `BUDDY_STT_AUDIO_CTX` (default: what it was trained for) in
`buddy.env`, restarts ears and shows its "loaded" line. Back: `install ggml-base.en.bin --ctx 0`.

## Data from real use (recommended)

Recorded sessions are you reading prompts at the desk. Real use is how you actually talk: from across the room, with
the box playing, phrasings the corpus doesn't have. ears can keep every command it transcribes, and those clips train
whisper like any session.

```
 Pi: ears ── wake word → command → whisper ──► ~/buddy/voice-live/<time>.wav + .txt (what whisper heard)
 PC: deploy/train.sh history
       pull ◄──rsync── voice-live            → ~/buddy-voice/live/
       label: play, Enter / correct / d / s  → session sessions/live/ (set train)
       delete the handled clips on the Pi
     deploy/train.sh                          trains on all sessions incl. live, evals, asks to install
```

**What is saved:** only the command after a wake word that whisper turned into text: no audio before the wake word,
no wake word itself, nothing when you said nothing. Newest `BUDDY_EARS_SAVE_MAX` (default 500, ~100 KB each, ~50 MB),
older ones are deleted by ears. It's your voice and whatever was said in the room after a (false) wake word: personal
data, never in Git (D16).

### Turn on (once)

**Pi**, in `~/buddy/config/buddy.env` (absolute path: systemd doesn't expand `~`):

```
BUDDY_EARS_SAVE_DIR=/home/dev/buddy/voice-live
```

`systemctl --user restart buddy@ears`. Check after the next command: `ls ~/buddy/voice-live`. Off: remove the line,
restart ears.

### Collect and label: `deploy/train.sh history`

Every week or two (at 500 unlabelled clips the oldest get lost):

1. **pull**: the Pi's saved commands → `~/buddy-voice/live/` (and any new recorded sessions).
2. **label**: plays each clip (if the PC can play audio: `paplay`, `aplay` or `ffplay`) and shows what whisper heard.
   - Enter: the transcript is right.
   - Type the correct sentence: written like the corpus (digits for numbers, names spelled like in Spotify).
   - `d`: not a command (false wake word, someone else talking, cut off): thrown away.
   - `s`: decide later (stays, comes back next time). `q`: stop here.
3. **delete on the Pi** the clips you answered (kept or thrown away), also after `q` or Ctrl-C. Otherwise the next pull
   would bring them back to label twice.

Kept clips are appended to the session `live` (`~/buddy-voice/sessions/live/`, numbered on), all as `train`.

Label honestly: the label is what you *said*, not what you meant. A wrong label teaches whisper the wrong thing, so
`d` when you can't hear it clearly. Commands whisper got wrong are the valuable ones.

### Retrain and compare: `deploy/train.sh`

Same as after recording (above): trains on **all** sessions (recorded + `live`), then evals the new model against the
installed one on the Pi and asks whether to install it. Live clips are never test clips: the test sessions stay the
fixed yardstick, so the before/after numbers stay comparable. Install only if intent clearly improves (box sessions
especially); otherwise keep collecting.

Worth a run at ~100 new live clips (`deploy/train.sh check` lists the count per session). After the Jabra moves (2.6),
the old clips still help, but record a fresh test session first so the eval measures the new position.

## When to retrain

- New artists or playlists whisper gets wrong (add sentences to the corpus, record a short session).
- The mic sounds different: Jabra moved into the body (Phase 2.6), another room.
- Enough live clips labelled (~100, `deploy/train.sh history`).

Each run trains on all sessions; nothing is ever thrown away.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `uv missing` | Install uv (setup table) |
| `no CUDA GPU: training on the CPU` | Driver/WSL GPU missing: `nvidia-smi` must work; `BUDDY_TORCH=cpu` only for smoke tests |
| `CUDA out of memory` | `--batch 8` (small.en: 4) |
| check: `no speech found` | Recording empty or Enter pressed twice quickly: re-record (`r`), or delete the row in `metadata.csv` |
| check: many `too quiet` | Closer, louder, or the Jabra's mic gain (`wpctl set-volume @DEFAULT_AUDIO_SOURCE@ 1.0`) |
| `kept epoch 0` (no improvement) | Too little or too uniform data: more sessions, more conditions |
| eval: `no output (model missing…)` | Name not in `~/buddy/models`; pass the local path instead |
| eval: garbage with `ctx=auto` | Stock model or one trained with `--ctx full`: use `ctx=0` |
| New model worse on new sentences | Overfitted: more varied clips, `--epochs 5` |
| record: no sound in the clips | `wpctl status` on the Pi: Jabra the default source (`*`), not muted (summary 1.4) |
