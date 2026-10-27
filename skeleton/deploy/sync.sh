#!/usr/bin/env bash
# Mirror the skeleton to the Pi's ~/buddy/src (deploy.sh and test.sh --pi build from there).
#
# Env: BUDDY_HOST (default "buddy", the alias from ~/.ssh/config)
set -euo pipefail

HOST="${BUDDY_HOST:-buddy}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [[ -n "$(git -C "$ROOT" status --porcelain)" ]]; then
    echo "⚠  syncing uncommitted changes" >&2
fi

echo "→ sync → $HOST:~/buddy/src"
ssh "$HOST" 'mkdir -p ~/buddy/src'
rsync -az --delete --progress \
    --exclude '.git/' --exclude '.idea/' --exclude 'cmake-build-*/' --exclude '/build*/' \
    --exclude '.venv/' --exclude '__pycache__/' \
    "$ROOT/" "$HOST:buddy/src/"
