# 1.11 (optional) – Speech-to-text trained on your voice

> Written 2026-10-23. **Not run yet.** I tested `stt-eval.sh` against a fake `buddy-ears`. I wrote the Colab cells from the Hugging Face Whisper API and whisper.cpp's `convert-h5-to-ggml.py` (read on GitHub today), but I haven't executed them. Items marked ⚠️ are assumptions.

## Goal

A whisper model fine-tuned on your voice, your accent and your names (artists, playlists), so commands that whisper keeps mishearing come out right. It is trained once in Google Colab, like the custom wake word. No Python runs on the Pi (D5); the only Python is the Colab notebook next to this guide. `buddy-ears` loads the result like any other `ggml-*.bin`.

**Done when:** `stt-eval.sh` scores the new model clearly better than the stock one on clips it wasn't trained on.

## Prerequisites

- 1.11 main guide done: `buddy_aec_source` exists, and ears uses it.
- You know which commands go wrong (`journalctl --user-unit buddy@ears | grep '"'` shows what whisper heard).
- A Google account (the free Colab T4 GPU is enough).

## Parts / tools

| Tool | Where | Role |
|---|---|---|
| `deploy/record-voice.sh` | Pi | Shows a sentence, records you saying it (16 kHz mono, echo-cancelled mic) |
| `deploy/voice-prompts.txt` | → `~/buddy/voice-data/prompts.txt` | The sentences: edit them to your artists and playlists |
| `deploy/stt-eval.sh` + `buddy-ears --transcribe` | Pi | Scores models on held-out clips |
| Google Colab, T4 GPU | Browser | Fine-tunes `openai/whisper-base.en`, converts it to GGML |
| whisper.cpp `models/convert-h5-to-ggml.py` | Cloned in Colab | Hugging Face model → `ggml-*.bin` for whisper.cpp |

## Background

- **What fine-tuning does:** whisper was trained on 680 000 hours of other people. A few hundred clips of *you* saying *Buddy's* sentences shift it a little toward your voice and vocabulary. It's cheap: base has 74 M parameters, and a few minutes on a T4 is enough.
- **Why record through Buddy:** the model should learn the sound ears actually gets, through the Jabra and the echo canceller, in your room. Phone recordings sound different.
- **Risk: forgetting.** A model trained too hard on 50 sentences gets worse at everything else. The guide uses a small learning rate and few epochs, keeps the epoch with the best validation score, and checks the result on a separate test set. Keep the new model only if it wins.
- **Cheaper first:** for one or two names, `BUDDY_STT_PROMPT=Spotify, Buddy, Daft Punk, Chill Vibes` (vocabulary hint) is often enough. Try that before training.
- **Model size:** train the same size you run (`BUDDY_STT_MODEL`, default `ggml-base.en.bin` → `openai/whisper-base.en`). For `tiny.en` / `small.en`, change `BASE` in cell 1.

## Steps

Commands run in your SSH session on the Pi unless marked **on the PC**.

### 1. Write your sentences

```bash
bash ~/buddy/src/deploy/record-voice.sh
micro ~/buddy/voice-data/prompts.txt
```

The first run only creates `prompts.txt`. Replace the example artists, songs and playlists with ones you'd really ask for, especially those whisper gets wrong. Write each line the way it should be transcribed ("Play Around the World by Daft Punk."). Aim for 50–80 lines.

### 2. Record the training set

```bash
bash ~/buddy/src/deploy/record-voice.sh
```

Per sentence: Enter → say it → Enter. `s` skips, `q` quits; the next run continues numbering. **Do three runs** (≈ 150–250 clips, ~10 min each), varying how you record:

1. From your desk chair, normal voice.
2. From 2–3 m, a bit louder.
3. With music playing quietly on Buddy (the echo canceller's leftovers are part of what ears hears).

Listen to one clip at random: `pw-play ~/buddy/voice-data/clips/00007.wav`.

### 3. Record a test set

Use different sentences than in training, or at least a different day and position, so the score means something:

```bash
mkdir -p ~/buddy/voice-test
cp ~/buddy/voice-data/prompts.txt ~/buddy/voice-test/prompts.txt
micro ~/buddy/voice-test/prompts.txt      # rephrase lines, add 5–10 new ones
bash ~/buddy/src/deploy/record-voice.sh ~/buddy/voice-test
```

### 4. Baseline score

```bash
bash ~/buddy/src/deploy/stt-eval.sh ~/buddy/voice-test ggml-base.en.bin
```

Expected shape:

```
40 clips

== ggml-base.en.bin
  ✗ 00012.wav
      said: Play my Chill Vibes playlist.
     heard: Play my chilled vibes playlist.
  → 33 / 40 right, avg stt 850 ms
```

Note the score. If it's already 40/40, you don't need this guide.

### 5. Copy the clips to the PC

```bash
tar czf ~/voice-data.tar.gz -C ~/buddy voice-data
```

**On the PC:** `scp buddy:voice-data.tar.gz .`

### 6. Train in Colab

Open <https://colab.research.google.com> → **File → Upload notebook** → `guides/01-11-custom-stt.ipynb` (asks for a T4 GPU; check **Runtime → Change runtime type** if not), then run the cells in order. The notebook holds exactly the cells below; if you change one, change both.

**Cell 1 – setup**

```python
!pip -q install jiwer soundfile
import copy, csv, random, re
import jiwer, soundfile as sf, torch
from transformers import WhisperForConditionalGeneration, WhisperProcessor

BASE = "openai/whisper-base.en"  # same size as BUDDY_STT_MODEL
dev = "cuda"
```

**Cell 2 – upload** (pick `voice-data.tar.gz`)

```python
from google.colab import files
files.upload()
!tar xzf voice-data.tar.gz
```

**Cell 3 – load the clips**

```python
proc = WhisperProcessor.from_pretrained(BASE)
rows = list(csv.DictReader(open("voice-data/metadata.csv")))
for r in rows:
    audio, sr = sf.read("voice-data/" + r["file_name"], dtype="float32")
    assert sr == 16000, r["file_name"]
    r["x"] = proc.feature_extractor(audio, sampling_rate=16000, return_tensors="pt").input_features[0]
random.seed(0)
random.shuffle(rows)
n_val = max(10, len(rows) // 7)
val, train = rows[:n_val], rows[n_val:]
print(len(train), "train,", len(val), "validation")
```

**Cell 4 – score before training**

```python
model = WhisperForConditionalGeneration.from_pretrained(BASE).to(dev)

def norm(t):
    return re.sub(r"\s+", " ", re.sub(r"[^a-z0-9 ]", " ", t.lower())).strip()

def score(rows):
    model.eval()
    heard = []
    with torch.no_grad():
        for i in range(0, len(rows), 16):
            x = torch.stack([r["x"] for r in rows[i:i + 16]]).to(dev)
            heard += proc.batch_decode(model.generate(x, max_new_tokens=64), skip_special_tokens=True)
    return jiwer.wer([norm(r["transcription"]) for r in rows], [norm(h) for h in heard]), heard

before, heard = score(val)
print(f"before: {before:.1%} word error rate")
for r, h in zip(val, heard):
    if norm(r["transcription"]) != norm(h):
        print(" ", r["transcription"], "→", h)
```

**Cell 5 – train** (≈ 2–5 min ⚠️)

```python
def labels(rows):
    ids = [proc.tokenizer(r["transcription"]).input_ids for r in rows]
    y = torch.full((len(ids), max(map(len, ids))), -100)  # -100 = padding, ignored by the loss
    for i, t in enumerate(ids):
        y[i, :len(t)] = torch.tensor(t)
    if (y[:, 0] == model.config.decoder_start_token_id).all():
        y = y[:, 1:]  # the model puts the start token in front itself
    return y

model.config.apply_spec_augment = True  # randomly masks bits of the spectrogram: less overfitting ⚠️
opt = torch.optim.AdamW(model.parameters(), lr=1e-5, weight_decay=0.01)
EPOCHS, BATCH = 5, 8
best = (before, copy.deepcopy(model.state_dict()))
for epoch in range(EPOCHS):
    model.train()
    random.shuffle(train)
    for i in range(0, len(train), BATCH):
        b = train[i:i + BATCH]
        x = torch.stack([r["x"] for r in b]).to(dev)
        loss = model(input_features=x, labels=labels(b).to(dev)).loss
        loss.backward()
        opt.step()
        opt.zero_grad()
    wer, _ = score(val)
    print(f"epoch {epoch + 1}: loss {loss.item():.3f}, word error rate {wer:.1%}")
    if wer < best[0]:
        best = (wer, copy.deepcopy(model.state_dict()))
model.load_state_dict(best[1])
print(f"kept: {best[0]:.1%} (before {before:.1%})")
```

Expected: the error rate drops in the first epochs, then stays flat. If "kept" equals "before", training didn't help. Record more varied clips and try again.

**Cell 6 – convert to GGML and download**

```python
model.save_pretrained("buddy-whisper")
proc.save_pretrained("buddy-whisper")
!git clone -q --depth 1 https://github.com/openai/whisper
!git clone -q --depth 1 https://github.com/ggml-org/whisper.cpp
!python whisper.cpp/models/convert-h5-to-ggml.py buddy-whisper whisper .
!mv ggml-model.bin ggml-buddy-base.en.bin
files.download("ggml-buddy-base.en.bin")
```

Expected: `Done. Output file:  ggml-model.bin`, then a ~140 MB download (f16, same size as the stock base model).

### 7. Compare on the Pi

**On the PC:** `scp ggml-buddy-base.en.bin buddy:buddy/models/`

```bash
bash ~/buddy/src/deploy/stt-eval.sh ~/buddy/voice-test ggml-base.en.bin ggml-buddy-base.en.bin
```

Keep it only if it's clearly better, and say a few sentences that are **not** in your prompts too ("play something relaxing", a new artist).

### 8. Use it

In `~/buddy/config/buddy.env`:

```
BUDDY_STT_MODEL=ggml-buddy-base.en.bin
```

```bash
systemctl --user restart buddy@ears
journalctl --user-unit buddy@ears -n 3     # loaded …/ggml-buddy-base.en.bin
```

To go back: `BUDDY_STT_MODEL=ggml-base.en.bin`, restart.

## Verify

- [ ] `~/buddy/voice-data/metadata.csv` has ≥ 150 clips, and a random clip sounds clean.
- [ ] Colab: "kept" < "before".
- [ ] `stt-eval.sh`: the new model beats the stock one on `voice-test`.
- [ ] Live: the commands that used to fail now work, and other commands still do.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `record-voice.sh`: `pw-record: … target not found` ⚠️ | No echo-cancel node | 1.11 main guide step 2, or `BUDDY_RECORD_SOURCE=` (empty = default mic) |
| Clips silent or very short | Enter pressed twice too fast | Delete the clip's line in `metadata.csv` and the file (`s` before a recording skips a sentence) |
| Cell 3: `AssertionError … wav` | Clip not 16 kHz | Only use `record-voice.sh` clips |
| Cell 5: `CUDA out of memory` | Batch too large (small.en) | `BATCH = 4` |
| Cell 6: `No such file … mel_filters.npz` | openai/whisper clone failed | Rerun the clone line |
| `stt-eval.sh`: "no output" | Model file not in `~/buddy/models`, or wrong name | `ls ~/buddy/models` |
| New model worse on new sentences | Overfitted | Fewer epochs (`EPOCHS = 3`), more and more varied clips |
| Names still wrong | Too few examples of them | 5+ different sentences per name, in all three recording runs |

## Notes for later phases

- **Retrain** when you add many new artists or playlists, or after the body changes how the mic sounds (Phase 2.6). Keep `voice-data/` around: new runs add to it.
- **German:** the same steps work with `openai/whisper-base` (multilingual) and German sentences; set `BUDDY_STT_LANGUAGE=de`.
- **Back up** the dataset (**on the PC:** `scp buddy:voice-data.tar.gz .` again): it's the only part you can't regenerate.
