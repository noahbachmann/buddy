# 1.8 + 1.9 – Brain (intent router) & Spotify skill

> Written 2026-10-23. Tested in the build sandbox: the brain's rules on 36 sample transcripts; the Spotify player logic against a mock Web API (device lookup, token refresh after 401, transfer, search, volume); the PKCE hash against OpenSSL. **Not yet run against the real Spotify API or Soloist.** On the Pi, librespot (first choice) connected but Spotify refused its audio keys, so 1.9 now uses Spotify Soloist (see Background). Items marked ⚠️ are assumptions.

Steps 1.8 and 1.9 share one guide: the brain is only testable end-to-end once a skill exists.

## Goal

"Hey Jarvis, play Spotify / pause / next song / louder / play Daft Punk" controls music that plays from the Jabra.

- **1.8 done when:** transcripts are routed to the right skill topic (`buddy/spotify/cmd`).
- **1.9 done when:** "Hey buddy, play Spotify / pause / next song" works.

## Prerequisites

- 1.7 done: transcripts arrive on `buddy/ears/transcript`.
- **Spotify Premium** on your account. Since February 2026, Development Mode apps only work if the app owner has Premium, and the Soloist API key needs Premium too.
- A phone or PC with the Spotify app, on the same Wi-Fi as the Pi (once, to pair Soloist).

## Parts / tools

| Tool | Where | Role |
|---|---|---|
| `buddy-brain` | Pi, `services/brain` | Transcript → intent → command for a skill |
| `buddy-spotify` | Pi, `services/spotify` | Commands → Spotify Web API (remote control) |
| Spotify Soloist (current build from Spotify) | Pi, `~/buddy/bin/soloist`, user unit `buddy-soloist`, weekly `buddy-soloist-update.timer` | The Spotify Connect device "Buddy": logs in, streams, plays through PipeWire |
| libcurl (`libcurl4-openssl-dev`) | Pi (apt) | HTTPS client for the Web API |
| Spotify developer app | developer.spotify.com | Client ID for the Web API |

## Background

```
 ears ──transcript──► brain ──buddy/spotify/cmd──► spotify ──HTTPS──► Spotify Web API
                        │                            │                      │ "play on device Buddy"
                        └─► buddy/voice/say          └─► buddy/voice/say    ▼
                            ("Sorry, …")                 ("Playing …")    Soloist ──► PipeWire ──► Jabra
```

- **Two Spotify parts, two jobs.** Soloist *is* the speaker: Spotify's official headless Spotify Connect client (released August 2026), like a smart speaker without a box. It shows up as "Buddy" in your Spotify app and plays audio. The Web API is the *remote control*: our `spotify` service tells Spotify's servers "play X on the device Buddy", and they tell Soloist. Soloist is a closed binary, installed like Mosquitto; our code stays C++ (D5).
- **Why not librespot.** The first version used librespot (open source, via raspotify). It connected, but every track failed with `error audio key 0 1` / `Service unavailable { audio key error }`: Spotify's servers refuse the decryption key to librespot for many accounts, mostly but not only newer ones ([librespot #1649](https://github.com/librespot-org/librespot/issues/1649)). No client-side fix exists. Soloist uses Spotify's own playback engine, so it isn't affected. `setup-pi.sh` removes the old librespot setup.
- **How Soloist runs here.** As the user unit `buddy-soloist` (a user unit reaches `dev`'s PipeWire session, see 1.4; Soloist plays via PulseAudio protocol through `pipewire-pulse`). Spotify doesn't allow redistributing it, and **builds expire after 90 days** (exit code 10), so `deploy/soloist-update.sh` downloads the current build from Spotify, and a weekly timer repeats that. It needs an **API key** from the developer dashboard (Premium). Its cache is limited to 512 MB to spare the microSD ⚠️.
- **Closed source, so sandboxed.** Soloist isn't open source: the GitHub repo `spotify/soloist` only holds a README, and nobody outside Spotify can check what the binary does beyond streaming (e.g. what it reports back). So `buddy-soloist.service` runs it in a systemd sandbox: your home directory is replaced by an empty one with only `~/.local/share/soloist` (pairing) and `~/.cache/soloist` (cache) mapped in, so it **can't read** `buddy.env`, the Spotify token, `~/.ssh` or anything else of yours. The rest of the system is read-only, it can't gain privileges, and only normal network and system calls are allowed. Its API key therefore lives in its own file, `~/buddy/config/soloist.env`. The update script never runs the binary outside the sandbox either (it compares checksums instead of asking for the version). Still visible to it: the network (it must stream) and the PipeWire socket (it must play). ⚠️ The sandbox options weren't tested on the Pi: if Soloist won't start, see Troubleshooting.
- **Logging in, two separate logins:**
  1. *Soloist* pairs once via Spotify Connect discovery: you pick "Buddy" in the Spotify app, and it stores the login itself.
  2. *buddy-spotify* gets a Web API token via OAuth with PKCE. You log in once in a browser and it stores a **refresh token** in `~/buddy/config/spotify-token.json` (mode 600). It uses that token to get a fresh 1-hour access token whenever needed. PKCE means no client secret is stored on the Pi.
- **Headless OAuth trick.** Spotify only allows `http://` redirects to a loopback IP literal like `http://127.0.0.1:8888/callback` (`localhost` isn't accepted). Nothing listens there: after login, your PC's browser shows "can't connect", and you copy the URL from its address bar (it contains `?code=…`) back into the terminal.
- **Development Mode limits** (since Feb 2026): the owner needs Premium; at most 5 users per app; several endpoints were removed or are being removed (mostly catalog metadata like new releases and top tracks); search returns at most 10 results. The player endpoints and search used here are not on the removal lists ⚠️ (verify in the [migration guide](https://developer.spotify.com/documentation/web-api/tutorials/february-2026-migration-guide) if something returns 403/404).
- **Rules, not AI (golden rule 3).** The brain lowercases the transcript, strips punctuation and politeness ("can you … please"), fixes whisper misspellings of "Spotify", then tries regex rules that must match the **whole** sentence. "pause" matches; "I don't want to pause anything right now" doesn't. The rules sit behind an `IntentParser` interface so an LLM can replace them later without touching routing.
- **Who talks.** Whoever knows the outcome sends the reply: the brain says "Sorry, I didn't catch that."; spotify says "Playing Daft Punk." or the error. Successful play/pause/next/volume stay silent: the music is the answer. Until 1.10 nothing listens on `buddy/voice/say`, so it's silent for now.

### Commands Buddy understands

| You say (examples) | Intent | Web API call |
|---|---|---|
| "play Spotify", "play", "play music", "resume", "continue" | `play` | Resume on Buddy; if Buddy isn't active yet, transfer playback to it |
| "play Daft Punk", "play Bohemian Rhapsody", "play Peach by Kevin Abstract", "play chill playlist" | `play` + `query` | Search (top 5): exact artist name → artist; else the track whose title matches best; "<song> by <artist>" filters by both; "… playlist" → playlists only |
| "play my playlist Chill Vibes", "play my Chill Vibes playlist" | `play_playlist` + `query` | Your own/followed playlists (`GET /me/playlists`): exact name, else the one containing the most spoken words; a word 1 letter off counts too ("type" finds "Hype"). whisper's "play mode playlist" is read as "my" |
| "play my playlist" | `play_playlist` | `BUDDY_SPOTIFY_DEFAULT_URI` (step 8) |
| "pause", "stop", "stop the music" | `pause` | `PUT /me/player/pause` |
| "next", "skip", "next song", "play the next song" | `next` | `POST /me/player/next` |
| "previous", "go back", "last song" | `previous` | `POST /me/player/previous` |
| "louder", "turn it up", "volume up" | `volume_up` | +10 % (`BUDDY_SPOTIFY_VOLUME_STEP`) |
| "quieter", "softer", "turn it down" | `volume_down` | −10 % |

### MQTT contract

```
buddy/brain/intent     {"intent":"play","query":"daft punk","skill":"spotify","text":"Play Daft Punk."}
buddy/brain/intent     {"intent":"unknown","text":"What time is it?"}
buddy/spotify/cmd      {"action":"play","query":"daft punk"}        (QoS 1)
buddy/spotify/state    {"playing":true,"volume":60}                 (retained; null until known)
buddy/voice/say        {"text":"Playing Daft Punk."}                (QoS 1)
```

### Decisions made in this step

| Topic | Choice | Why |
|---|---|---|
| Spotify playback | Spotify Soloist as sandboxed user unit `buddy-soloist`, updated weekly, key in `soloist.env` | librespot gets no audio keys on many accounts; Soloist is official but closed source, so it only sees its own files |
| Web API auth | Authorization Code + PKCE, refresh token in `~/buddy/config/spotify-token.json` | No secret on the Pi; works headless |
| HTTP client | libcurl (apt) | Standard, arm64 package |
| Intents | Regex rules behind `IntentParser` | Simple first (golden rule 3); swappable for an LLM later |
| Replies | The service that knows the outcome publishes `buddy/voice/say` | The brain can't know whether a search found something |

## Steps

### 1. Update the Pi
 
The build now needs libcurl, and (for 1.10) libpiper. `--setup` installs both plus Spotify Soloist. **On your PC**, in the repo (all other commands in this guide run on the Pi, in your SSH session):

```bash
git pull
deploy/deploy.sh --setup
```

New in the setup output:

```
→ libpiper 1.8.0 → /opt/libpiper           (first time: several minutes, compiles espeak-ng)
→ removing librespot (replaced by Soloist)   (only if the old setup ran)
→ Spotify Soloist
  ✓ soloist (new) → 3f9a…   (first 12 hex digits of the binary's SHA-256)
  ⚠ SOLOIST_API_KEY not set in ~/buddy/config/soloist.env: buddy-soloist not started
→ models (wake word, VAD, speech-to-text, voice)
  ✓ en_US-lessac-medium.onnx     61M
...
  buddy@ears       active
  buddy@brain      active
  buddy@spotify    failed      ← expected: BUDDY_SPOTIFY_CLIENT_ID not set yet (step 3)
  buddy@voice      active
```

⚠️ `buddy@spotify` keeps restarting until step 3. That's harmless; after five attempts systemd gives up until the next deploy.

### 2. Create the Spotify developer app and the Soloist key

1. Open <https://developer.spotify.com/dashboard>, log in with the **Premium** account, accept the terms.
2. **Spotify Soloist API Key** page (in the dashboard menu): accept the terms, **generate** a key, copy it. It's a secret: don't paste it into chats or Git.
3. Back in the dashboard: **Create app**: name `Buddy`, description `Desk robot`, Redirect URI exactly `http://127.0.0.1:8888/callback` (click **Add**), API: tick **Web API**. Save.
4. App → **Settings** → copy the **Client ID**. You don't need the client secret.
5. ⚠️ If the login in step 5 fails with "user not registered": Settings → **User Management** → add your own account's name and email.

### 3. Configure

The Soloist key goes into its **own** file (only the sandboxed Soloist reads it):

```bash
nano ~/buddy/config/soloist.env
```

```bash
SOLOIST_API_KEY=<the Soloist key>
SOLOIST_DEVICE_NAME=Buddy
```

The Client ID goes into `buddy.env` (uncomment the line):

```bash
nano ~/buddy/config/buddy.env
```

```bash
BUDDY_SPOTIFY_CLIENT_ID=0123456789abcdef0123456789abcdef
```

If you rename the device, change `SOLOIST_DEVICE_NAME` and `BUDDY_SPOTIFY_DEVICE` together.

### 4. Start Soloist, pair it, test the audio path

```bash
systemctl --user restart buddy-soloist
journalctl --user-unit buddy-soloist -n 30 --no-pager
```

Expected: no errors, `systemctl --user is-active buddy-soloist` → `active`. Check that the sandbox is on: `systemd-analyze --user security buddy-soloist | tail -1` should rate it around `OK` or better, not `UNSAFE` ⚠️. Now on your phone/PC (same Wi-Fi): Spotify → play any song → the device icon (speaker symbol) → **Buddy**. The music should come from the Jabra within a few seconds.

This proves Soloist, PipeWire and the Jabra work, independent of our code. Soloist stays paired across restarts. Leave it paused for now.

### 5. Log buddy-spotify in (once)

Manual runs don't get systemd's `EnvironmentFile`, so load `buddy.env` first:

```bash
set -a; . ~/buddy/config/buddy.env; set +a
~/buddy/bin/buddy-spotify --login
```

Expected:

```
1. Open this URL in a browser and log in to Spotify:

https://accounts.spotify.com/authorize?response_type=code&client_id=…&code_challenge=…

2. The browser then fails to load http://127.0.0.1:8888/callback?code=… (expected).
   Copy the full URL from its address bar and paste it here:
>
```

Open the URL on your PC, log in, click **Agree**. The browser shows "This site can't be reached": copy the whole URL (`http://127.0.0.1:8888/callback?code=AQ…&state=…`), paste it, press Enter:

```
✓ logged in, token saved to /home/dev/buddy/config/spotify-token.json
```

### 6. Offline tests

**Brain** (no MQTT, no Spotify):

```bash
~/buddy/bin/buddy-brain --test "Play Spotify." "Pause." "Play Daft Punk." "Turn it up." "What time is it?"
```

Expected:

```
"Play Spotify." → {"intent":"play","skill":"spotify","text":"Play Spotify."}
"Pause." → {"intent":"pause","skill":"spotify","text":"Pause."}
"Play Daft Punk." → {"intent":"play","query":"daft punk","skill":"spotify","text":"Play Daft Punk."}
"Turn it up." → {"intent":"volume_up","skill":"spotify","text":"Turn it up."}
"What time is it?" → {"intent":"unknown","text":"What time is it?"}
```

**Spotify** (real API, no MQTT). One command per line; listen to the Jabra between them:

```bash
set -a; . ~/buddy/config/buddy.env; set +a
~/buddy/bin/buddy-spotify --cmd play                  # ok            (resumes on Buddy)
~/buddy/bin/buddy-spotify --cmd volume_down           # ok
~/buddy/bin/buddy-spotify --cmd next                  # ok
~/buddy/bin/buddy-spotify --cmd play "daft punk"      # ok, says: "Playing Daft Punk."
~/buddy/bin/buddy-spotify --cmd pause                 # ok
```

A failure prints the reason first, e.g.:

```
[spotify] play: device "Buddy" not found (buddy-soloist not running or not paired?)
failed, says: "Spotify isn't running on Buddy."
```

### 7. Live, end to end

```bash
systemctl --user restart buddy@spotify
mosquitto_sub -v -t 'buddy/#'
```

Say "Hey Jarvis, play Spotify". Expected (other lines omitted):

```
buddy/ears/transcript {"audio_ms":1840,"language":"en","stt_ms":910,"text":"Play Spotify."}
buddy/brain/intent {"intent":"play","skill":"spotify","text":"Play Spotify."}
buddy/spotify/cmd {"action":"play"}
buddy/spotify/state {"playing":true,"volume":null}
```

Then "Hey Jarvis, next song", "… louder", "… pause", "… play Daft Punk". Timing per command in the log:

```bash
journalctl --user-unit buddy@spotify -n 20 --no-pager
# [spotify] play "daft punk" ok (450 ms)   ⚠️ expected 200–600 ms: device lookup + search + play
```

### 8. Optional: default playlist

"Play my playlist" (no name) plays it, and plain "play" falls back to it when there's nothing to resume (new account, long idle). Take the playlist's share link, e.g. `https://open.spotify.com/playlist/4DESbYIkRWMwxvqBYjVygh?si=…`: the part between `playlist/` and `?` is the ID. In `buddy.env`:

```bash
BUDDY_SPOTIFY_DEFAULT_URI=spotify:playlist:37i9dQZF1DXcBWIGoYBM5M
```

## Verify

- [ ] Spotify app → device "Buddy" plays through the Jabra (step 4).
- [ ] `buddy-brain --test` gives the expected intents (step 6).
- [ ] `buddy-spotify --cmd play/pause/next` work (step 6).
- [ ] Live: "play Spotify", "pause", "next song", "louder", "quieter" each work ≥ 9 of 10 times from 2–3 m.
- [ ] Live: "play <artist>" starts that artist.
- [ ] Nonsense after the wake word → `{"intent":"unknown"}` on `buddy/brain/intent` and `{"text":"Sorry, …"}` on `buddy/voice/say`.
- [ ] After `sudo reboot`: "play Spotify" works without touching anything (Soloist pairing and token survive).

**Done when:** "Hey buddy, play Spotify / pause / next song" works.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| CMake: `Could NOT find CURL` or `LIBPIPER_INCLUDE_DIR-NOTFOUND` | Setup not re-run | On the PC: `deploy/deploy.sh --setup` |
| "Buddy" missing in the Spotify app | Soloist not running, or phone on another network | `systemctl --user status buddy-soloist`; `journalctl --user-unit buddy-soloist -n 30`; same Wi-Fi |
| `SOLOIST_API_KEY is not set` in the Soloist log | Key missing in `soloist.env` | Step 3, then `systemctl --user restart buddy-soloist` |
| Soloist fails at start with `status=226/NAMESPACE`, `218/CAPABILITIES` or `Failed to set up mount namespacing` | A sandbox option isn't supported in a user unit on this kernel ⚠️ | `journalctl --user-unit buddy-soloist -n 30`. On the PC, in `deploy/systemd/buddy-soloist.service`, comment out sandbox blocks one at a time (start with `ProtectHome`/`Bind*Paths`, then `PrivateDevices`), run `deploy/deploy.sh`, retry. Tell Claude which one, so it can be fixed properly |
| Soloist runs but logs `Read-only file system` / `Permission denied` | It writes somewhere outside its two folders | Note the path from the log; add it to `BindPaths=` only if it's harmless (a Soloist-specific folder) |
| Soloist pauses right after "playing"; log: `Failed to create secure directory (/run/user/1000/pulse)` | Sandbox hid the PulseAudio socket (unit before the 2026-10-23 fix) | On the PC: `git pull`, `deploy/deploy.sh`; then `systemctl --user restart buddy-soloist` |
| Soloist runs but plays nothing; log mentions PulseAudio/connection refused | pipewire-pulse not running | `systemctl --user status pipewire-pulse.socket`; `ls /run/user/1000/pulse/native` |
| Soloist log: key rejected ⚠️ | Wrong/revoked key | Generate a new one (step 2.2) |
| `buddy-soloist` stopped, exit code 10 | Build expired (90 days) and the timer didn't run | `bash ~/buddy/src/deploy/soloist-update.sh` |
| Old librespot error `error audio key 0 1` | Spotify refuses keys to librespot | Fixed by switching to Soloist: on the PC `git pull`, `deploy/deploy.sh --setup` |
| `journalctl --user …`: `No journal files were found` | The Pi keeps the journal in RAM: no per-user journal files | Use `journalctl --user-unit <unit>` (on the PC: `deploy/logs.sh <unit>`) |
| Soloist plays but nothing is heard | PipeWire session missing or wrong sink | `wpctl status` while playing: Soloist must appear under Streams, Jabra default sink (1.4 step 4) |
| `BUDDY_SPOTIFY_CLIENT_ID is not set` | Manual run without loading `buddy.env`, or not set | `set -a; . ~/buddy/config/buddy.env; set +a` |
| Login page: `INVALID_CLIENT: Invalid client` / "client_id: Invalid" | Wrong value in `BUDDY_SPOTIFY_CLIENT_ID`: the client secret or Soloist key instead of the Client ID, or hidden characters | `grep SPOTIFY_CLIENT_ID ~/buddy/config/buddy.env \| cat -A` must show 32 characters of `0-9a-f` then `$` (no `^M`, spaces or quotes). Compare with dashboard → app → Settings → Client ID. `buddy-spotify` warns at start if the format is off |
| Login page: `INVALID_CLIENT: Invalid redirect URI` | Redirect URI differs from the dashboard | Must be exactly `http://127.0.0.1:8888/callback` in both places (no trailing slash) |
| `state mismatch` | Pasted the URL of an older login attempt | Run `--login` again, use only its URL |
| `not logged in: run buddy-spotify --login` | No token file | Step 5 |
| "I'm not allowed to read your playlists yet" | Token from a login before the playlist permission was added (2026-10-23) | Step 5 again (`--login`); Spotify asks to approve the new permission |
| `token request rejected … invalid_grant` | Refresh token revoked (app access removed, password changed) | Step 5 again |
| `device "Buddy" not found` | Soloist down or not paired | `systemctl --user restart buddy-soloist`; redo step 4 |
| `HTTP 404 NO_ACTIVE_DEVICE` / "There's nothing to play" | Nothing to resume | Step 8, or start something once from the app |
| `PREMIUM_REQUIRED` | Account or app owner has no Premium | Premium on the dashboard account |
| `HTTP 403` on every call | Account not in User Management, or 5-user cap | Step 2.5 |
| `HTTP 429` | Rate limit | Wait; shouldn't happen at voice-command rates |
| Wrong intent ("play the next song" searches a song) | Rule order or missing phrasing | Add the phrase to `services/brain/rules.cpp`, test with `--test` |
| Artist or song names misheard ("James Groves" for "James Gruntz") | whisper doesn't know rare names; not an audio-level problem if ears logs voice ≥ ~20 dB above background | Vocabulary hint in `buddy.env`: `BUDDY_STT_PROMPT=Play my playlist Hype. Play City Lights by James Gruntz. Pause. Next song.` (short; artist and playlist names you use often), `systemctl --user restart buddy@ears`. Still bad: `ggml-small.en.bin` (slower, 1.7 step 2) |
| "play AC/DC" plays a cover | Search picked a track, not the artist | Artist matching ignores punctuation; say the exact artist name. Search quality is Spotify's |

## Notes for later phases

- **1.10 voice:** subscribes to `buddy/voice/say`; the replies already flow.
- **1.11 ducking:** on `buddy/ears/wake`, lower the music (Web API volume or PipeWire stream volume), restore after `buddy/ears/state` → `idle`. `buddy/spotify/state` currently only knows what *we* did; for playback changes made from the phone, Soloist's local WebSocket API (`--ws 127.0.0.1:24879`, already on) emits real playback events.
- **1.12:** `buddy-soloist` depends on the user's PipeWire like ears, and waits up to 2 min for DNS at boot (Soloist restores its login only once). Test after a cold boot.
- **LLM brain later:** implement `IntentParser` (`services/brain/intent.hpp`) with a model; routing and topics stay the same. Keep the rules as a fast path.
- **German:** rules are English. For `BUDDY_STT_LANGUAGE=de`, add German patterns ("spiel", "pause", "nächstes Lied", "lauter", "leiser") in `rules.cpp`.
- **2.x motion:** `buddy/brain/intent` is the hook for gestures ("wave" → `skill: motion`). Add a route in `command_topic()` in `services/brain/main.cpp`.
- **Spotify policy risk:** Development Mode rules changed twice in 2026. If endpoints disappear, the Spotify skill is the only service affected; Soloist keeps working as a plain Connect speaker. Soloist's WebSocket API (play URI, pause, volume, queue) could also replace most Web API calls; only search needs the Web API.
