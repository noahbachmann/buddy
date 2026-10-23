# skeleton/ – C++ code quick reference

CMake root (`cmake -S skeleton`). One executable per service, all linked against `buddy_shared`. Roadmap, decisions and
conventions: `../CLAUDE.md`.

## Service pattern (every `services/<name>/main.cpp`)

1. `install_signal_handlers()` (`lifecycle.hpp`): SIGINT/SIGTERM → `running()` false.
2. Parse test flags first (`--wav`, `--test`, `--say`, `--cmd`, `--duck`): these run without MQTT and print instead of publishing.
3. `struct Config` reads `BUDDY_*` env (`config.hpp`: `env_string/int/float`, typos throw → exit 1 before MQTT).
4. Load models/devices inside one try block, exit 1 on failure.
5. `Mqtt mqtt("<name>", mqtt_config_from_env())`, `subscribe(…)`, `start()` (sends READY, blocks until connected).
6. Main loop: `systemd::notify_watchdog()` + `BlockingQueue::pop(200 ms)` or sleep; slow work never on Paho's thread.
7. Publish final retained state, `mqtt.stop()`.

## shared/

| File | What |
|---|---|
| `topics.hpp/.cpp` | All topic names (single source of truth) + wildcard `matches()` |
| `mqtt.hpp/.cpp` | Paho wrapper: client id `buddy-<svc>`, retained status + last will, resubscribe on reconnect, backoff, exceptions contained |
| `systemd.hpp/.cpp` | READY / WATCHDOG (rate-limited) / STATUS via `$NOTIFY_SOCKET`; no-op outside systemd |
| `queue.hpp` | `BlockingQueue<T>`: Paho callback → service thread |
| `config.hpp/.cpp` | Env lookups, trims whitespace and `\r` |

## Services

| Service | Key files | Offline entry point |
|---|---|---|
| ears | `audio_source` (ALSA capture thread, reopens device), `wake_word` (3 ONNX models), `vad` (Silero + endpointer + level gate), `stt` (whisper.cpp) | `--wav f.wav`, `--transcribe f.wav…` |
| brain | `rules` (normalize + regex `RuleParser` behind `IntentParser`) | `--test "text"…` |
| spotify | `http` (libcurl, keep-alive), `auth` (PKCE, token file), `player` (Web API; `warm_up()` on `ears/wake`) | `--login`, `--cmd <action> [query]` |
| voice | `tts` (libpiper, sentence streaming), `audio_sink` (ALSA per utterance / WAV) | `--say "…" [--wav out.wav]` |
| mixer | `pactl set-sink-volume` on `buddy_music`, event-driven hold timer | `--duck`, `--restore` |
| ping/pong | 1.5 template, not deployed | – |

## Gotchas

- Third-party code (`third_party/`) gets no project warning flags; whisper is forced to `-O2` even in Debug.
- `/opt/onnxruntime` and `/opt/libpiper` are imported targets with hand-written paths (their CMake configs are broken); binaries keep the rpath.
- A new main loop without `notify_watchdog()` gets killed by systemd every 60 s.
- Two copies of one service (CLion + unit) share the client id and kick each other off the broker.
