# skeleton/ – code quick reference

Project root for CMake (C++) and the uv workspace (Python). One executable per service, `buddy@<name>` on the Pi.
Roadmap, decisions and conventions: `../CLAUDE.md`.

## Service pattern (C++: `services/<name>/main.cpp`; Python: same steps with `buddy_shared`)

1. `install_signal_handlers()` (`lifecycle.hpp` / `buddy_shared.lifecycle`): SIGINT/SIGTERM → `running()` false.
2. Parse test flags first (`--wav`, `--transcribe`, `--test`, `--say`, `--cmd`, `--login`): no MQTT, print instead of publishing.
3. Config from `BUDDY_*` env (`config.hpp` `env_string/int/float` / `buddy_shared.config`): typos throw → exit 1 before MQTT.
4. Load models/devices inside one try block, exit 1 on failure.
5. `Mqtt("<name>", …)`, `subscribe(…)`, `start()`: sends READY, blocks until connected.
6. Main loop: `notify_watchdog()` + `BlockingQueue::pop(200 ms)` or sleep; slow work never on Paho's thread.
7. Publish final retained state, `mqtt.stop()`.

## shared/

| File | What |
|---|---|
| `topics.json` | All topic names (single source). CMake generates `build/shared/include/buddy/topics.hpp` from it (`topics.hpp.in`) |
| `src/topics.cpp` | Wildcard `matches()` |
| `mqtt.*` | Paho wrapper: client id `buddy-<svc>`, retained status + last will, resubscribe on reconnect, backoff, exceptions contained |
| `systemd.*` | READY / WATCHDOG (rate-limited) / STATUS via `$NOTIFY_SOCKET`; no-op outside systemd |
| `queue.hpp` | `BlockingQueue<T>`: Paho callback → service thread |
| `config.*` | Env lookups, trims whitespace and `\r` |
| `python/buddy_shared/` | The same for Python: `topics` (reads topics.json), `mqtt` (paho-mqtt 2), `systemd`, `config`, `lifecycle` |

## Services

| Service | Key files | Offline entry point |
|---|---|---|
| ears | `audio_source` (ALSA capture thread, reopens device; WAV read/write), `wake_word` (3 ONNX models), `vad` (Silero + endpointer + level gate), `stt` (whisper.cpp; `audio_ctx` auto = `auto_audio_ctx()`, shared with tools/voice) | `--wav f.wav`, `--transcribe f.wav…` |
| brain | `rules` (normalize + regex `RuleParser` behind `IntentParser`), `test_corpus.cpp` (ctest) | `--test "text"…` |
| spotify | `http` (libcurl, keep-alive), `auth` (PKCE, token file), `player` (Web API; target = named speaker (`speaker()`, fuzzy) / box / active device, own-playlist cache, duck/restore by device id with tmpfs recovery file) | `--login`, `--cmd <action> [query] [speaker]`, `--cmd devices\|duck\|restore` |
| voice | `tts` (libpiper, sentence streaming), `audio_sink` (ALSA per utterance / WAV) | `--say "…" [--wav out.wav]` |

## tools/voice (Python, runs on the PC; `deploy/record.sh`, `deploy/train.sh`)

| File | What |
|---|---|
| `commands.tsv` | Corpus: set, sentence, intent, query, device. Prompts, brain ctest, intent scoring |
| `buddy_voice/data.py` | `~/buddy-voice` layout, sessions, import of old record-voice.sh data, check, trimmed cache |
| `vad.py`, `audio.py` | Silero trimming like ears; WAV I/O; `auto_audio_ctx` (= `stt.hpp`, tested) |
| `train.py` | HF Whisper fine-tune: sentence-level validation split, short-context encoder (`short_context()`), greedy validation |
| `convert.py` | GGML via whisper.cpp's converter (same release as CMake, tested) + q8_0 |
| `evaluate.py`, `pi.py` | Scores on the Pi via ssh (`buddy-ears --transcribe`, `buddy-brain --test`), install |

## Tests

- `ctest` (build dir): `brain_corpus` (every train/test sentence in `commands.tsv`), `topics`. Not built by deploy.sh.
- `uv run pytest` (here): `buddy_shared` (MQTT test needs a broker on localhost), `buddy_voice` (corpus, scoring, shared constants with C++/CMake).
- VM: `uv sync --all-packages --extra cpu`; `BUDDY_TORCH=cpu deploy/train.sh --base tiny.en --max-steps 2 --no-pull --no-eval` smoke-tests training.

## Gotchas

- Third-party code (`third_party/`) gets no project warning flags; whisper is forced to `-O2` even in Debug.
- `/opt/onnxruntime` and `/opt/libpiper` are imported targets with hand-written paths (their CMake configs are broken); binaries keep the rpath.
- A new main loop without `notify_watchdog()` gets killed by systemd every 60 s.
- Two copies of one service (CLion + unit) share the client id and kick each other off the broker.
- uv workspace members are listed explicitly (uv needs a pyproject.toml in every glob match, C++ service dirs have none).
- `train.py` replaces `WhisperEncoder.forward` of the pinned transformers version: re-check it when bumping transformers.
- `const json`'s `operator[]` asserts on a missing key: spotify reads device fields with `field()`.
- `BUDDY_STT_AUDIO_CTX=auto` breaks stock whisper models (hallucination loops); only models trained with `--ctx auto`.
