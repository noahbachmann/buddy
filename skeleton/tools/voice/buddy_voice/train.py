"""Fine-tune Whisper on your voice (CUDA), then convert it for ears.

Choices, and why:
- Split by sentence, not by clip: every sentence is recorded in several sessions, so a random clip split would
  validate on sentences the model trained on (the old notebook did that; its "best epoch" was optimistic).
- Short audio context (ctx="auto", the default): the encoder sees only as many frames as the command needs, exactly
  like ears with BUDDY_STT_AUDIO_CTX=auto. That makes speech-to-text ~5× less encoder work on the Pi. Stock Whisper
  gets worse with a short context; a model trained with it doesn't (FUTO's "audio context fine-tuning" idea, done
  here directly on the transcripts). Training adds a random extra margin so the model doesn't rely on the exact length.
- SpecAugment, small learning rate, early stopping on validation WER: a few hundred clips overfit fast.
- Greedy decoding without timestamps for validation, like ears (services/ears/stt.cpp).
The test set (set=test) is never used here: deploy/train.sh scores it on the Pi with whisper.cpp afterwards.
"""

import copy
import hashlib
import json
import random
import time
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
import torch
from torch import nn
from transformers import WhisperForConditionalGeneration, WhisperProcessor
from transformers.modeling_outputs import BaseModelOutput

from . import data
from .audio import auto_audio_ctx, pad_short, read_wav
from .convert import quantize, to_ggml
from .score import norm, word_errors
from .vad import Vad


@dataclass
class Config:
    base: str = "base.en"       # = BUDDY_STT_MODEL's size (ggml-base.en.bin → base.en)
    source: str = "raw"         # raw (what ears hears) | aec (old sessions only) | both
    ctx: str = "auto"           # auto | full
    epochs: int = 10
    patience: int = 3           # epochs without validation improvement before stopping
    lr: float = 1e-5
    batch: int = 16
    val_percent: int = 15
    extra_margin: int = 32      # training only: random 0..N extra encoder frames (ctx=auto)
    max_steps: int = 0          # >0: stop after this many steps (smoke test)
    quantize: bool = True
    seed: int = 0
    sessions: list[str] = field(default_factory=list)  # empty = all

    @property
    def repo(self) -> str:
        return f"openai/whisper-{self.base}"


@dataclass
class Item:
    clip_id: str
    sentence: str
    features: torch.Tensor  # (80, 3000) float16, log-mel of the 30 s window like Whisper
    frames: int             # auto_audio_ctx of the trimmed clip
    labels: list[int]


def short_context(encoder: nn.Module) -> None:
    """Lets the encoder take fewer than 3000 mel frames, like whisper.cpp's audio_ctx: the position embeddings are
    cut to the length. Replaces WhisperEncoder.forward of transformers 5.19 (pinned in pyproject.toml)."""

    def forward(input_features, attention_mask=None, **kwargs):
        x = nn.functional.gelu(encoder.conv1(input_features))
        x = nn.functional.gelu(encoder.conv2(x)).permute(0, 2, 1)
        x = x + encoder.embed_positions.weight[: x.shape[1]]
        x = nn.functional.dropout(x, p=encoder.dropout, training=encoder.training)
        for layer in encoder.layers:
            if encoder.training and torch.rand([]) < encoder.layerdrop:
                continue
            x = layer(x, None, **kwargs)
        return BaseModelOutput(last_hidden_state=encoder.layer_norm(x))

    encoder.forward = forward


def is_validation(sentence: str, percent: int) -> bool:
    """Stable per sentence across runs and sessions."""
    return int(hashlib.sha1(norm(sentence).encode()).hexdigest(), 16) % 100 < percent


def load_items(cfg: Config, proc: WhisperProcessor, vad: Vad) -> tuple[list[Item], list[Item], list[str]]:
    sources = ["aec", "raw"] if cfg.source == "both" else [cfg.source]
    train, val, skipped = [], [], []
    for clip in data.clips(("train",), cfg.sessions or None):
        for source in sources:
            path = data.trimmed(clip, source, vad)
            if path is None:
                if clip.path(source).exists():
                    skipped.append(f"{clip.id} ({source}): no speech")
                continue
            audio = pad_short(read_wav(path))
            feats = proc.feature_extractor(audio, sampling_rate=16000, return_tensors="pt").input_features[0]
            ids = proc.tokenizer(clip.sentence).input_ids[1:]  # without <|startoftranscript|>: the model prepends it
            item = Item(f"{clip.id}:{source}", clip.sentence, feats.half(), auto_audio_ctx(len(audio)), ids)
            (val if is_validation(clip.sentence, cfg.val_percent) else train).append(item)
    return train, val, skipped


def batch_features(items: list[Item], ctx: str, extra: int = 0) -> torch.Tensor:
    feats = torch.stack([i.features for i in items]).float()
    if ctx == "full":
        return feats
    frames = min(max(i.frames for i in items) + extra, 1500)
    return feats[:, :, : 2 * frames]  # 2 mel frames per encoder frame (conv2 stride)


def batch_labels(items: list[Item]) -> torch.Tensor:
    y = torch.full((len(items), max(len(i.labels) for i in items)), -100)  # -100 = ignored by the loss
    for row, i in enumerate(items):
        y[row, : len(i.labels)] = torch.tensor(i.labels)
    return y


@torch.no_grad()
def transcribe(model, proc, items: list[Item], ctx: str, device: str, batch: int = 32) -> list[str]:
    model.eval()
    tok = proc.tokenizer
    prefix = tok.prefix_tokens  # <|startoftranscript|>[<|en|><|transcribe|>]<|notimestamps|>
    eot = tok.eos_token_id
    out = []
    for start in range(0, len(items), batch):
        chunk = items[start : start + batch]
        enc = model.model.encoder(batch_features(chunk, ctx).to(device))
        ids = torch.tensor([prefix] * len(chunk), device=device)
        done = torch.zeros(len(chunk), dtype=torch.bool, device=device)
        for _ in range(48):  # commands are short
            logits = model(encoder_outputs=enc, decoder_input_ids=ids).logits[:, -1]
            logits[:, eot + 1 :] = -float("inf")  # no special tokens or timestamps, only text or the end
            nxt = torch.where(done, eot, logits.argmax(-1))
            ids = torch.cat([ids, nxt[:, None]], dim=1)
            done |= nxt == eot
            if done.all():
                break
        out += [t.strip() for t in tok.batch_decode(ids, skip_special_tokens=True)]
    return out


def wer(items: list[Item], heard: list[str]) -> float:
    errors = [word_errors(i.sentence, h) for i, h in zip(items, heard)]
    return sum(e for e, _ in errors) / max(1, sum(n for _, n in errors))


def run(cfg: Config) -> Path:
    random.seed(cfg.seed)
    torch.manual_seed(cfg.seed)
    device = "cuda" if torch.cuda.is_available() else "cpu"
    if device == "cpu":
        print("⚠ no CUDA GPU: training on the CPU is very slow (fine for a smoke test with --max-steps)")
    bf16 = device == "cuda" and torch.cuda.is_bf16_supported()

    stamp = time.strftime("%Y%m%d-%H%M")
    run_dir = data.DATA / "runs" / f"{stamp}-{cfg.base}"
    run_dir.mkdir(parents=True)
    proc = WhisperProcessor.from_pretrained(cfg.repo)
    if cfg.base.endswith(".en"):
        proc.tokenizer.set_prefix_tokens(predict_timestamps=False)
    else:  # multilingual: English for now (BUDDY_STT_LANGUAGE)
        proc.tokenizer.set_prefix_tokens(language="en", task="transcribe", predict_timestamps=False)
    vad = Vad(data.CACHE)
    train, val, skipped = load_items(cfg, proc, vad)
    for s in skipped:
        print("  skipped", s)
    if not train or not val:
        raise SystemExit(f"need training and validation clips, got {len(train)} / {len(val)}: record more sessions")
    print(f"{len(train)} training clips, {len(val)} validation clips ({len({i.sentence for i in val})} sentences "
          f"never trained on), {device}{' bf16' if bf16 else ''}, ctx {cfg.ctx}")

    model = WhisperForConditionalGeneration.from_pretrained(cfg.repo).to(device)
    short_context(model.model.encoder)
    model.config.apply_spec_augment = True
    model.config.mask_time_prob, model.config.mask_time_length = 0.05, 10
    model.config.mask_feature_prob, model.config.mask_feature_length = 0.05, 10

    before = wer(val, transcribe(model, proc, val, cfg.ctx, device))
    print(f"validation WER before: {before:.1%}", flush=True)

    steps_per_epoch = (len(train) + cfg.batch - 1) // cfg.batch
    total = cfg.max_steps or steps_per_epoch * cfg.epochs
    opt = torch.optim.AdamW(model.parameters(), lr=cfg.lr, weight_decay=0.01)
    warmup = max(1, total // 10)
    sched = torch.optim.lr_scheduler.LambdaLR(
        opt, lambda s: min((s + 1) / warmup, max(0.0, (total - s) / max(1, total - warmup))))

    best = (before, 0, copy.deepcopy(model.state_dict()))
    history, step, stale = [], 0, 0
    for epoch in range(1, cfg.epochs + 1):
        model.train()
        random.shuffle(train)
        losses = []
        for start in range(0, len(train), cfg.batch):
            chunk = train[start : start + cfg.batch]
            extra = random.randint(0, cfg.extra_margin) if cfg.ctx == "auto" else 0
            with torch.autocast(device, dtype=torch.bfloat16, enabled=bf16):
                loss = model(input_features=batch_features(chunk, cfg.ctx, extra).to(device),
                             labels=batch_labels(chunk).to(device)).loss
            loss.backward()
            nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            opt.step()
            sched.step()
            opt.zero_grad()
            losses.append(loss.item())
            step += 1
            if step >= total:
                break
        heard = transcribe(model, proc, val, cfg.ctx, device)
        score = wer(val, heard)
        history.append({"epoch": epoch, "loss": float(np.mean(losses)), "val_wer": score})
        print(f"epoch {epoch}: loss {np.mean(losses):.3f}, validation WER {score:.1%}", flush=True)
        if score < best[0]:
            best, stale = (score, epoch, copy.deepcopy(model.state_dict())), 0
        else:
            stale += 1
        if stale >= cfg.patience or step >= total:
            break

    model.load_state_dict(best[2])
    print(f"kept epoch {best[1]}: validation WER {best[0]:.1%} (before {before:.1%})")
    if best[1] == 0:
        print("⚠ training didn't beat the stock model on validation: more and more varied clips needed")
    heard = transcribe(model, proc, val, cfg.ctx, device)
    for i, h in zip(val, heard):
        if norm(i.sentence) != norm(h):
            print(f"    said: {i.sentence}\n   heard: {h}")

    hf_dir = run_dir / "hf"
    model.save_pretrained(hf_dir)
    proc.save_pretrained(hf_dir)
    ggml = to_ggml(hf_dir, cfg.repo, run_dir / f"ggml-buddy-{cfg.base}-{stamp}.bin", data.CACHE)
    q8 = quantize(ggml, data.CACHE) if cfg.quantize else None
    report = {
        "base": cfg.base, "ctx": cfg.ctx, "source": cfg.source, "lr": cfg.lr, "batch": cfg.batch,
        "sessions": cfg.sessions or data.session_names(),
        "train_clips": len(train), "val_clips": len(val), "val_wer_before": before, "val_wer": best[0],
        "best_epoch": best[1], "history": history, "models": [p.name for p in (ggml, q8) if p],
    }
    (run_dir / "report.json").write_text(json.dumps(report, indent=2))
    for p in (ggml, q8):
        if p:
            p.with_suffix(".json").write_text(json.dumps({"ctx": cfg.ctx, "run": run_dir.name}))
    print(f"✓ {ggml}" + (f"\n✓ {q8}" if q8 else ""))
    return ggml
