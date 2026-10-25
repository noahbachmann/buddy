#!/usr/bin/env bash
# Speech-to-text training on the PC (docs/voice-training.md): pull the recordings from the Pi, check them,
# fine-tune whisper on the GPU, convert it for ears, score it on the Pi against the current model, install it.
#
# Usage: deploy/train.sh [train options]        the whole pipeline (deploy/train.sh --help for options)
#        deploy/train.sh <command> [options]    one step: pull, check, eval, install, label, import
#   e.g. deploy/train.sh eval current ggml-base.en.bin --ctx 0,auto
#
# Python with uv (tools/voice). Env: BUDDY_HOST (default "buddy"), BUDDY_VOICE_DIR (default ~/buddy-voice),
# BUDDY_TORCH (default cuda; cpu for machines without an NVIDIA GPU, smoke tests only)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
command -v uv > /dev/null || { echo "uv missing: https://docs.astral.sh/uv/getting-started/installation/" >&2; exit 1; }
[[ $# -eq 0 || "$1" == -* ]] && set -- train "$@"
exec uv run --project "$ROOT" --package buddy-voice --extra "${BUDDY_TORCH:-cuda}" buddy-voice "$@"
