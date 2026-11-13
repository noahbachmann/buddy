# Voice retrain (#67) — temporary, delete when done

Start after Claude ticks the Claude part of #67. All from `skeleton/` on the PC.

1. `deploy/train.sh history` — label saved commands: Enter = right, type what you said, `d` = junk.
2. `deploy/record.sh desk new` — only the new sentences (~10 min). Optional, music on: `deploy/record.sh box new`.
3. `deploy/record.sh test2 test-new` — test session, ideally another day.
4. `deploy/train.sh` — trains, scores, asks: `y` if **intent** is higher than the current model.
5. Try "all lights off" a few times.

Later: every ~100 real commands, repeat 1 + 4. Details: `docs/voice-training.md`.
