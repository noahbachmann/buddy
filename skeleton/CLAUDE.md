# skeleton/ – code quick reference

Project root for CMake (C++) and the uv workspace (Python). One executable per service, `buddy@<name>` on the Pi.
Roadmap, decisions and conventions: `../CLAUDE.md`.

## Service pattern (C++: `services/<name>/main.cpp`; Python: same steps with `buddy_shared`)

1. `install_signal_handlers()` (`lifecycle.*` / `buddy_shared.lifecycle`): SIGINT/SIGTERM → `running()` false,
   `on_stop()` callbacks, `sleep()` wakes.
2. Parse test flags first (`--wav`, `--transcribe`, `--test`, `--say`, `--cmd`, `--login`): no MQTT, print instead of publishing.
3. Config from `BUDDY_*` env (`config.hpp` `env_string/int/float` / `buddy_shared.config`): typos throw → exit 1 before MQTT.
4. Load models/devices inside one try block, exit 1 on failure.
5. `Mqtt("<name>", …)`, `subscribe(…)`, `start()`: sends READY, blocks until connected.
6. Main loop: `notify_watchdog()` + `BlockingQueue::pop(5 s)` with `on_stop([&] { queue.close(); })`, or `sleep(5s)`:
   idle services don't wake to poll `running()` (voice, spotify still `pop(200ms)`). Slow work never on Paho's thread.
7. Publish final retained state, `mqtt.stop()`.

## shared/

| File | What |
|---|---|
| `topics.json` | All topic names (single source). CMake generates `build/shared/include/buddy/topics.hpp` from it (`topics.hpp.in`), incl. `topic::commands` (command topics in use and not `"web": false`: what web may publish) |
| `src/topics.cpp` | Wildcard `matches()` |
| `mqtt.*` | Paho wrapper: client id `buddy-<svc>`, retained status + last will, resubscribe on reconnect, backoff, exceptions contained |
| `lifecycle.*` | Signal handler → eventfd → stop thread: `on_stop()` callbacks, wakes `sleep()`; works whichever thread takes the signal |
| `systemd.*` | READY / WATCHDOG (rate-limited) / STATUS via `$NOTIFY_SOCKET`; no-op outside systemd |
| `queue.hpp` | `BlockingQueue<T>`: Paho callback → service thread; `close()` wakes `pop()` for good |
| `config.*` | Env lookups, trims whitespace and `\r` |
| `http.*`, `curl_http.*` | `Http` interface + `url_encode` (`buddy_http`, no libcurl: logic and tests); `CurlHttp` (`buddy_curl_http`): keep-alive, own CA (PEM blob), `CURLOPT_RESOLVE`, `stream()` for server-sent events |
| `motion_protocol.hpp` | Protocol v1 Pi ⇄ Pico (header only, constexpr, no heap/exceptions): `parse_request`/`format_*` for the firmware, `format_request`/`parse_message`/`parse_hello` for motion (2.3) |
| `names.*` | `buddy_names`: fuzzy matching of spoken names (`best_matches`, `match_score`): spotify speakers/playlists, hue rooms/lights |
| `python/buddy_shared/` | The same for Python: `topics` (reads topics.json), `mqtt` (paho-mqtt 2), `systemd`, `config`, `lifecycle` |

## Services

| Service | Key files | Offline entry point |
|---|---|---|
| ears | `audio_source` (ALSA capture thread, reopens device; WAV read/write), `wake_word` (3 ONNX models), `vad` (Silero), `endpoint` (endpointer + level gate on a `SpeechModel` interface, no ONNX = `ears_core`), `stt` (whisper.cpp; `audio_ctx` auto = `auto_audio_ctx()`, shared with tools/voice), `transcript` (hallucination guards: `max_tokens` by length, repeated first sentence dropped; in `ears_core`) | `--wav f.wav`, `--transcribe f.wav…` |
| brain | `rules` (normalize + regex `RuleParser` behind `IntentParser`; light rules (skill hue) first, need a light word, `light_device()` turns "the kitchen lights" into "kitchen", `parse_room()` splits "… in the kitchen") = `brain_core` | `--test "text"…` |
| spotify | `auth` (PKCE, token file), `player` (Web API; target = named speaker (`speaker()`, `names::best_matches`) / box / active device, own-playlist cache, duck/restore by device id with tmpfs recovery file) | `--login`, `--cmd <action> [query] [speaker]`, `--cmd devices\|duck\|restore` |
| hue | `bridge` (CLIP v2 on `Http`: TLS by bridge id + compiled-in root CA, pairing, discovery, key file), `home` (rooms/zones/lights/scenes from `/clip/v2/resource`, eventstream updates, `find()` by spoken name: room > zone > light), `lights` (`plan()`: command → requests + reply, no I/O; `scene_save` snapshots a group's lights → POST scene, same name → PUT; `scene_delete`), `sse` (event parser), `throttle` (per-resource interval, merges piled-up bodies); main: eventstream thread → queue; PUTs via throttle, POST/DELETE direct | `--pair`, `--cmd <action> [query] [device]`, `--cmd list` |
| web | `state` (`StateStore`: last payload per topic + version; each change encoded once into shared SSE frames, a JSON Patch (`json::diff`) for streams that have the previous payload; `wait()` for event streams), `access` (Host allowlist against DNS rebinding, same-origin + JSON-only POSTs, command topics from `topic::commands`), `server` (cpp-httplib routes: static `www/`, `/api/state`, `/api/events` SSE, `POST /api/cmd/<svc>`) = `web_core`; `www/` vanilla HTML/CSS/JS, no build step; `applyPatch` (reconnects when out of step), `keyed()` rebuilds only children whose data changed; pages by `location.hash` (dashboard, `#hue` scenes); forms live in `index.html` (re-renders would drop typed text) | – (tests run a real server on a free port) |
| voice | `tts` (libpiper, sentence streaming), `audio_sink` (ALSA per utterance / WAV) | `--say "…" [--wav out.wav]` |
| motion | `joints` (table: name → channel/µs, `plan_move()` clamps + speed cap; `demo` on ch0 until 2.5), `gestures` (steps + spoken reply, `GesturePlayer` timing), `command` (cmd JSON), `serial_link` (termios + flock, reader thread, id-matched replies 500 ms, `* …` events queue, handshake HELLO → LIMITs, lost after I/O error / 3 timeouts) = `motion_core`; main: reconnect 1 → 10 s, PING 500 ms, unexpected `* BOOT` = re-handshake, `RELAX *` on stop | `--cmd move\|center\|relax\|gesture …`, `--raw "<id> <line>"` |

## firmware/pico-motion (Pico 2, own CMake project, `deploy.sh --firmware`)

| File | What |
|---|---|
| `CMakeLists.txt` | SDK 2.3.1 + TinyUSB as hashed tarballs before `project()`; picotool from `/opt/picotool`; `USBD_PRODUCT` → udev `/dev/buddy-pico`; warnings on our files only |
| `board.hpp` | Channel → GPIO (ch0 = GP2 …) |
| `servo.*` | PWM at 1 MHz tick / 50 Hz (level = µs, 0 = relaxed), limits, linear ramps, `update()` reports DONE |
| `main.cpp` | Line reader (bounded per loop), request handling, link watchdog (2 s → relax all), hardware watchdog 500 ms, `* BOOT` on first connect |

No unit tests on the Pico: the logic that can break (protocol) lives in the shared header and is tested on the host.

## tools/voice (Python, runs on the PC; `deploy/record.sh`, `deploy/train.sh`)

| File | What |
|---|---|
| `commands.tsv` | Corpus: set (train/test/wake/extra = brain test only), sentence, intent, query, device. Prompts, brain corpus test, intent scoring |
| `buddy_voice/data.py` | `~/buddy-voice` layout, sessions, import of old record-voice.sh data, check, trimmed cache |
| `vad.py`, `audio.py` | Silero trimming like ears; WAV I/O; `auto_audio_ctx` (= `stt.hpp`, both checked against `audio_ctx.json`) |
| `train.py` | HF Whisper fine-tune: sentence-level validation split, short-context encoder (`short_context()`), greedy validation |
| `convert.py` | GGML via whisper.cpp's converter (release from `third_party/whisper.json`, like CMake) + q8_0 |
| `evaluate.py`, `pi.py` | Scores on the Pi via ssh (`buddy-ears --transcribe`, `buddy-brain --test`), install |

## Tests

`deploy/test.sh [ctest args]`: C++ under ASan + UBSan (`build-test/`, target `buddy-tests` only: no ML libraries
built), pytest, shellcheck. `deploy/test.sh --pi`: the C++ tests on the Pi (`~/buddy/build-test`, no restarts).

| Where | What |
|---|---|
| `shared/tests/` | gtest: topics, config, motion protocol, queue, MQTT round trip (localhost broker, skipped without), systemd and lifecycle (own exes: process-wide state) |
| `shared/tests/cases/*.json` | Cases both languages check (`cases.hpp` / plain `json.load`): topic matching, `BUDDY_*` parsing, `audio_ctx` |
| `services/<svc>/tests/` | gtest, `test_<unit>.cpp` → `buddy-<svc>-tests`, linked against `<svc>_core` (the service's logic without main/hardware) |
| `services/motion/tests/test_serial_link.cpp` | `FakePico` on a pseudo-terminal (`openpty`): answers with the shared protocol code like the firmware |
| `shared/tests/fake_http.hpp` | `FakeHttp`: canned responses per `"METHOD /path"` (URL prefix → path mapping per service, e.g. `fake_spotify.hpp`), records requests; temp dir for token/state files |
| `shared/python/tests/`, `tools/voice/tests/` | pytest, `test_<module>.py` |

- `buddy_add_tests(<name> <files…> LIBS …)` (root CMakeLists): every TEST is its own ctest entry (and in CLion).
  `BUDDY_SOURCE_DIR` points at the skeleton for data files. deploy.sh builds with `BUDDY_TESTS=OFF`.
- Test logic that broke before or is invisible on the hardware: parsing, matching, state machines, file formats,
  protocols. Not: `main.cpp` glue, ALSA/Paho/libpiper/whisper wrappers, model quality (`train.sh eval`).
- A fact both languages need gets one source (topics.json, whisper.json, a cases file), never two copies plus a test.
- VM: `uv sync --all-packages --extra cpu` (train test needs PyTorch); `BUDDY_TORCH=cpu deploy/train.sh --base tiny.en --max-steps 2 --no-pull --no-eval` smoke-tests training.

## Gotchas

- Third-party code (`third_party/`) gets no project warning flags; whisper is forced to `-O2` even in Debug.
- `/opt/onnxruntime` and `/opt/libpiper` are imported targets with hand-written paths (their CMake configs are broken); binaries keep the rpath.
- A new main loop without `notify_watchdog()` gets killed by systemd every 60 s.
- Two copies of one service (CLion + unit) share the client id and kick each other off the broker.
- uv workspace members are listed explicitly (uv needs a pyproject.toml in every glob match, C++ service dirs have none).
- `train.py` replaces `WhisperEncoder.forward` of the pinned transformers version: `test_train.py` fails if a bump changes the encoder.
- `const json`'s `operator[]` asserts on a missing key: spotify reads device fields with `field()`.
- `for (… : make_json()["items"])` loops over a destroyed temporary (e0bf506): name the object first. ASan in
  `deploy/test.sh` catches it.
- `third_party/`: cpp-httplib pinned (FetchContent, header only, its CMakeLists skipped): the VM's apt has 0.14, the Pi's 0.18.
- web: an open `/api/events` stream holds one httplib worker thread; `StateStore::close()` ends them on shutdown.
- hue `Target` points into `Home`'s maps: use it before the next `load()`.
- hue fixture `services/hue/tests/resources.json` is hand-written in CLIP v2 shape; replace with `GET /clip/v2/resource` from the real bridge when its shape surprises.
- `BUDDY_STT_AUDIO_CTX=auto` breaks stock whisper models (hallucination loops); only models trained with `--ctx auto`.
