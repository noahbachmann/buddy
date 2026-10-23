# 1.8 + 1.9 – Brain (intent router) & Spotify skill

> Written 2026-10-23. Tested in the build sandbox: the brain's rules on 36 sample transcripts; the Spotify player logic against a mock Web API (device lookup, token refresh after 401, transfer, search, volume); the PKCE hash against OpenSSL. **Not yet run against the real Spotify API, librespot, or on the Pi.** Items marked ⚠️ are assumptions.

Steps 1.8 and 1.9 share one guide: the brain is only testable end-to-end once a skill exists.

## Goal

"Hey Jarvis, play Spotify / pause / next song / louder / play Daft Punk" controls music that plays from the Jabra.

- **1.8 done when:** transcripts are routed to the right skill topic (`buddy/spotify/cmd`).
- **1.9 done when:** "Hey buddy, play Spotify / pause / next song" works.

## Prerequisites

- 1.7 done: transcripts arrive on `buddy/ears/transcript`.
- **Spotify Premium** on your account. Since February 2026, Development Mode apps only work if the app owner has Premium, and librespot needs Premium anyway.
- A phone or PC with the Spotify app, on the same Wi-Fi as the Pi (once, to log librespot in).

## Parts / tools

| Tool | Where | Role |
|---|---|---|
| `buddy-brain` | Pi, `services/brain` | Transcript → intent → command for a skill |
| `buddy-spotify` | Pi, `services/spotify` | Commands → Spotify Web API (remote control) |
| librespot 0.8.0 (from the raspotify 0.48.4 `.deb`) | Pi, `/usr/bin/librespot`, user unit `buddy-librespot` | The Spotify Connect device "Buddy": logs in, streams, plays through PipeWire |
| libcurl (`libcurl4-openssl-dev`) | Pi (apt) | HTTPS client for the Web API |
| Spotify developer app | developer.spotify.com | Client ID for the Web API |

## Background

```
 ears ──transcript──► brain ──buddy/spotify/cmd──► spotify ──HTTPS──► Spotify Web API
                        │                            │                      │ "play on device Buddy"
                        └─► buddy/voice/say          └─► buddy/voice/say    ▼
                            ("Sorry, …")                 ("Playing …")    librespot ──► PipeWire ──► Jabra
```

- **Two Spotify parts, two jobs.** librespot *is* the speaker: an open-source Spotify Connect client, like a smart speaker. It shows up as "Buddy" in your Spotify app and plays audio. The Web API is the *remote control*: our `spotify` service tells Spotify's servers "play X on the device Buddy", and they tell librespot. librespot is Rust, installed as a binary like Mosquitto; our code stays C++ (D5).
- **Why raspotify's `.deb` but not its service.** raspotify packages a current librespot for arm64 (built with ALSA and PulseAudio backends). Its own service runs as a system user, which can't reach `dev`'s PipeWire session (see 1.4). So `setup-pi.sh` disables it and runs the same binary as the user unit `buddy-librespot` with `--backend pulseaudio` (PipeWire speaks the PulseAudio protocol through `pipewire-pulse`). Audio cache is off to spare the microSD.
- **Logging in, two separate logins:**
  1. *librespot* logs in once via Spotify Connect discovery: you pick "Buddy" in the Spotify app, and it caches the credentials in `~/.cache/librespot`.
  2. *buddy-spotify* gets a Web API token via OAuth with PKCE. You log in once in a browser and it stores a **refresh token** in `~/buddy/config/spotify-token.json` (mode 600). It uses that token to get a fresh 1-hour access token whenever needed. PKCE means no client secret is stored on the Pi.
- **Headless OAuth trick.** Spotify only allows `http://` redirects to a loopback IP literal like `http://127.0.0.1:8888/callback` (`localhost` isn't accepted). Nothing listens there: after login, your PC's browser shows "can't connect", and you copy the URL from its address bar (it contains `?code=…`) back into the terminal.
- **Development Mode limits** (since Feb 2026): the owner needs Premium; at most 5 users per app; several endpoints were removed or are being removed (mostly catalog metadata like new releases and top tracks); search returns at most 10 results. The player endpoints and search used here are not on the removal lists ⚠️ (verify in the [migration guide](https://developer.spotify.com/documentation/web-api/tutorials/february-2026-migration-guide) if something returns 403/404).
- **Rules, not AI (golden rule 3).** The brain lowercases the transcript, strips punctuation and politeness ("can you … please"), fixes whisper misspellings of "Spotify", then tries regex rules that must match the **whole** sentence. "pause" matches; "I don't want to pause anything right now" doesn't. The rules sit behind an `IntentParser` interface so an LLM can replace them later without touching routing.
- **Who talks.** Whoever knows the outcome sends the reply: the brain says "Sorry, I didn't catch that."; spotify says "Playing Daft Punk." or the error. Successful play/pause/next/volume stay silent: the music is the answer. Until 1.10 nothing listens on `buddy/voice/say`, so it's silent for now.

### Commands Buddy understands

| You say (examples) | Intent | Web API call |
|---|---|---|
| "play Spotify", "play", "play music", "resume", "continue" | `play` | Resume on Buddy; if Buddy isn't active yet, transfer playback to it |
| "play Daft Punk", "play Bohemian Rhapsody", "play chill playlist" | `play` + `query` | Search: exact artist name → artist; else best track; "… playlist" → playlists only |
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
| Spotify playback | librespot from the pinned raspotify `.deb`, as user unit `buddy-librespot` | Current arm64 build with the PulseAudio backend, no Rust toolchain; user unit reaches PipeWire |
| Web API auth | Authorization Code + PKCE, refresh token in `~/buddy/config/spotify-token.json` | No secret on the Pi; works headless |
| HTTP client | libcurl (apt) | Standard, arm64 package |
| Intents | Regex rules behind `IntentParser` | Simple first (golden rule 3); swappable for an LLM later |
| Replies | The service that knows the outcome publishes `buddy/voice/say` | The brain can't know whether a search found something |

## Steps

### 1. Update the Pi

The build now needs libcurl, and (for 1.10) libpiper. `--setup` installs both plus librespot. From the repo on your PC:

```bash
git pull
deploy/deploy.sh --setup
```

New in the setup output:

```
→ libpiper 1.8.0 → /opt/libpiper           (first time: several minutes, compiles espeak-ng)
→ librespot (raspotify 0.48.4)
→ librespot user unit
→ models (wake word, VAD, speech-to-text, voice)
  ✓ en_US-lessac-medium.onnx     61M
...
  buddy@ears       active
  buddy@brain      active
  buddy@spotify    failed      ← expected: BUDDY_SPOTIFY_CLIENT_ID not set yet (step 4)
  buddy@voice      active
```

⚠️ `buddy@spotify` keeps restarting until step 4. That's harmless; after five attempts systemd gives up until the next deploy.

### 2. Log librespot in and test the audio path

```bash
ssh buddy systemctl --user status buddy-librespot --no-pager
```

Expected: `active (running)`. Now on your phone/PC: Spotify → play any song → the device icon (speaker symbol) → **Buddy**. The music should come from the Jabra within a few seconds.

This proves librespot, PipeWire and the Jabra work, independent of our code. librespot has now cached your credentials and logs in by itself after every restart:

```bash
ssh buddy ls ~/.cache/librespot     # credentials.json
```

Leave it paused for now.

### 3. Create the Spotify developer app

1. Open <https://developer.spotify.com/dashboard>, log in with the **Premium** account, accept the terms.
2. **Create app**: name `Buddy`, description `Desk robot`, Redirect URI exactly `http://127.0.0.1:8888/callback` (click **Add**), API: tick **Web API**. Save.
3. App → **Settings** → copy the **Client ID**. You don't need the client secret.
4. ⚠️ If the login in step 5 fails with "user not registered": Settings → **User Management** → add your own account's name and email.

### 4. Configure

```bash
ssh buddy nano ~/buddy/config/buddy.env
```

Add (uncomment) at least:

```bash
BUDDY_SPOTIFY_CLIENT_ID=0123456789abcdef0123456789abcdef
```

### 5. Log buddy-spotify in (once)

Manual runs don't get systemd's `EnvironmentFile`, so load `buddy.env` first:

```bash
ssh -t buddy 'set -a; . ~/buddy/config/buddy.env; set +a; ~/buddy/bin/buddy-spotify --login'
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
ssh buddy '~/buddy/bin/buddy-brain --test "Play Spotify." "Pause." "Play Daft Punk." "Turn it up." "What time is it?"'
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
ssh buddy
set -a; . ~/buddy/config/buddy.env; set +a
~/buddy/bin/buddy-spotify --cmd play                  # ok            (resumes on Buddy)
~/buddy/bin/buddy-spotify --cmd volume_down           # ok
~/buddy/bin/buddy-spotify --cmd next                  # ok
~/buddy/bin/buddy-spotify --cmd play "daft punk"      # ok, says: "Playing Daft Punk."
~/buddy/bin/buddy-spotify --cmd pause                 # ok
```

A failure prints the reason first, e.g.:

```
[spotify] play: device "Buddy" not found (librespot not running or not logged in?)
failed, says: "Spotify isn't running on Buddy."
```

### 7. Live, end to end

```bash
ssh buddy systemctl --user restart buddy@spotify
ssh buddy "mosquitto_sub -v -t 'buddy/#'"
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
deploy/logs.sh spotify
# [spotify] play "daft punk" ok (450 ms)   ⚠️ expected 200–600 ms: device lookup + search + play
```

### 8. Optional: something to play from scratch

"Play" resumes your last playback. If there's nothing to resume (new account, long idle), set a fallback playlist in `buddy.env` (share link → `spotify:playlist:<id>`):

```bash
BUDDY_SPOTIFY_DEFAULT_URI=spotify:playlist:37i9dQZF1DXcBWIGoYBM5M
```

## Verify

- [ ] Spotify app → device "Buddy" plays through the Jabra (step 2).
- [ ] `buddy-brain --test` gives the expected intents (step 6).
- [ ] `buddy-spotify --cmd play/pause/next` work (step 6).
- [ ] Live: "play Spotify", "pause", "next song", "louder", "quieter" each work ≥ 9 of 10 times from 2–3 m.
- [ ] Live: "play <artist>" starts that artist.
- [ ] Nonsense after the wake word → `{"intent":"unknown"}` on `buddy/brain/intent` and `{"text":"Sorry, …"}` on `buddy/voice/say`.
- [ ] After `ssh buddy sudo reboot`: "play Spotify" works without touching anything (librespot and token survive).

**Done when:** "Hey buddy, play Spotify / pause / next song" works.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| CMake: `Could NOT find CURL` or `LIBPIPER_INCLUDE_DIR-NOTFOUND` | Setup not re-run | `deploy/deploy.sh --setup` |
| "Buddy" missing in the Spotify app | librespot not running, or phone on another network | `systemctl --user status buddy-librespot`; same Wi-Fi; `avahi-daemon` running (`systemctl status avahi-daemon`) |
| librespot plays but nothing is heard | PipeWire session missing or wrong sink | `wpctl status` while playing: librespot must appear under Streams, Jabra default sink (1.4 step 4) |
| `BUDDY_SPOTIFY_CLIENT_ID is not set` | Manual run without loading `buddy.env`, or not set | `set -a; . ~/buddy/config/buddy.env; set +a` |
| Login page: `INVALID_CLIENT: Invalid redirect URI` | Redirect URI differs from the dashboard | Must be exactly `http://127.0.0.1:8888/callback` in both places (no trailing slash) |
| `state mismatch` | Pasted the URL of an older login attempt | Run `--login` again, use only its URL |
| `not logged in: run buddy-spotify --login` | No token file | Step 5 |
| `token request rejected … invalid_grant` | Refresh token revoked (app access removed, password changed) | Step 5 again |
| `device "Buddy" not found` | librespot down or logged out | `systemctl --user restart buddy-librespot`; redo step 2 |
| `HTTP 404 NO_ACTIVE_DEVICE` / "There's nothing to play" | Nothing to resume | Step 8, or start something once from the app |
| `PREMIUM_REQUIRED` | Account or app owner has no Premium | Premium on the dashboard account |
| `HTTP 403` on every call | Account not in User Management, or 5-user cap | Step 3.4 |
| `HTTP 429` | Rate limit | Wait; shouldn't happen at voice-command rates |
| Wrong intent ("play the next song" searches a song) | Rule order or missing phrasing | Add the phrase to `services/brain/rules.cpp`, test with `--test` |
| "play AC/DC" plays a cover | Search picked a track, not the artist | Artist matching ignores punctuation; say the exact artist name. Search quality is Spotify's |

## Notes for later phases

- **1.10 voice:** subscribes to `buddy/voice/say`; the replies already flow.
- **1.11 ducking:** on `buddy/ears/wake`, lower the music (Web API volume or PipeWire stream volume), restore after `buddy/ears/state` → `idle`. `buddy/spotify/state` currently only knows what *we* did; for playback changes made from the phone, librespot's `--onevent` hook could feed real state later.
- **1.12:** `buddy-librespot` depends on the user's PipeWire like ears; test both after a cold boot.
- **LLM brain later:** implement `IntentParser` (`services/brain/intent.hpp`) with a model; routing and topics stay the same. Keep the rules as a fast path.
- **German:** rules are English. For `BUDDY_STT_LANGUAGE=de`, add German patterns ("spiel", "pause", "nächstes Lied", "lauter", "leiser") in `rules.cpp`.
- **2.x motion:** `buddy/brain/intent` is the hook for gestures ("wave" → `skill: motion`). Add a route in `command_topic()` in `services/brain/main.cpp`.
- **Spotify policy risk:** Development Mode rules changed twice in 2026. If endpoints disappear, the Spotify skill is the only service affected; librespot keeps working as a plain Connect speaker.
