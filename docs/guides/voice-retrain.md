# Voice retrain (#67): temporary, delete when done

Goal: whisper hears your short commands ("All lights off.") the way you say them in real use. Claude's part is done
(brain fixes deployed, corpus 351 `train` / 76 `test` lines, `record.sh … new`, live score). Everything runs from
`skeleton/` on the PC. Details for every command: `docs/voice-training.md`.

## 1. Label the live commands (~15 min)

```bash
deploy/train.sh history
```

~100 saved commands since 2026-10-29. Each one plays, then shows what whisper heard: Enter = right, type what you
said (written like the corpus: "All lights off."), `d` = not a command / can't hear it, `q` = stop (the rest comes
back next time). Label what you *said*, not what you meant.

At the end, the **live score** of the installed model: write the intent line down, it's the baseline for step 5.

```
live score (90 commands): intent 61 / 75, exact 58 / 90, WER 14.2%
brain doesn't understand (tell Claude, or add them to commands.tsv):
  ? …
```

Anything under "brain doesn't understand" that you actually say → tell Claude **before step 2**: the fix may add
corpus lines that should be recorded too.

## 2. Record the new lines (~25 min, splittable)

```bash
deploy/record.sh desk new
```

Only the `train` sentences without a clip from before today: ~250 (the new short forms and every test word 5×), minus
whatever you labelled in step 1. `q` stops; the same command later the same day continues where you stopped. Speak
like you talk to Buddy, at the distance you usually do.

Recommended, the same day (only today's sessions are ignored, so it gets every new line again): music on the Nest at
normal volume, then

```bash
deploy/record.sh box new
```

Real use mostly has music playing; without a box session the new lines are learned only in silence.

## 3. Fresh test session (~3 min, ideally another day)

```bash
deploy/record.sh test2 test-new
deploy/record.sh test2-box test-new    # music on, same day
```

19 `test` sentences without a test clip yet (the new rephrasings plus light lines added after the last test session).
Never train on these: they're what the eval measures.

## 4. Train, compare, install (~15–30 min ⚠️ untested with this much data)

```bash
deploy/train.sh
```

Pulls, checks (fix `⚠` clips with `r` in the recorder or `--force`), trains, then the eval table: `current` vs. the
new model, one row per test session. Install (`1` / `2` at the prompt) only if **intent** is clearly higher overall
and not lower on the box sessions, and `ms avg` stays ≲ 300. Otherwise Enter keeps the current one; send Claude the
table.

## 5. Live check

- "Hey Jarvis, all lights off." / "… all lights on." 5× each, from where you usually sit, once with music. Expect
  every one to switch.
- After ~a week of normal use: `deploy/train.sh history` again. Its live score against step 1's baseline is the real
  verdict.

Then tick the Dev boxes in #67 and delete this file.
