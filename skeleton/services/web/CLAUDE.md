# services/web – web panel (D19)

HTTP ⇄ MQTT bridge plus a static page. No skill logic: the page sends the same commands brain does, the skills answer
with their retained state. User-facing behaviour and troubleshooting: `docs/01-voice.md` → 1.14.

## Server (C++, `web_core` + `main.cpp`)

| File | What |
|---|---|
| `main.cpp` | Config (`BUDDY_WEB_*`), MQTT subscribes `buddy/+/state` + `buddy/+/status` → `StateStore::set`, runs the server; on stop `store.close()` so open event streams return |
| `state.*` | `StateStore`: last payload per topic + version counter. Each change is encoded once into shared SSE frames: whole payload, or an RFC 6902 patch (`json::diff`) against the previous one when smaller. `wait(since)` gives a stream only what changed since it last looked |
| `access.*` | Host allowlist (IPs, `localhost`, `*.local`, `BUDDY_WEB_HOSTS`) against DNS rebinding; POSTs must be `application/json` and same-origin; `<svc>` must have a command topic in `topic::commands` (generated from `topics.json`, minus `"web": false`, e.g. motion) |
| `server.*` | cpp-httplib routes: `GET /api/state` (snapshot), `GET /api/events` (SSE, keepalive comments), `POST /api/cmd/<svc>` → publish `buddy/<svc>/cmd`, static files from `BUDDY_WEB_ROOT` (`~/buddy/share/web`, installed from `www/`) |

## Page (`www/`, vanilla JS, no build step, no tests)

`app.js` sections, top to bottom:

| Section | What |
|---|---|
| core | `state` (topic → payload), `el()` DOM builder, `keyed(container, [key, sig, make])`: rebuilds only children whose `sig` (JSON of their inputs) changed, `rebuild()` forgets that cache (snaps an optimistic switch back after a failed command) |
| talking to Buddy | `send(svc, cmd)` → POST, toast on error; `throttled()` for sliders (≤ 1 cmd per interval, always the last value); `hold()`/`held()`: a light slider's value wins over lagging hue states until reported (≤ 5 s); `applyPatch()`; `connect()`: EventSource, on a patch that doesn't fit it reconnects (new stream = full payloads) |
| widgets, lights | toggles/sliders (`interacting` > 0 blocks re-render while a finger holds a slider), light colour from `mirek`/`xy`, room cards, "All lights" bar, room layout (order/hidden) in `localStorage` key `buddy.lights.layout` |
| hue tab | `#hue`: scene editor and list (`scene_save` / `scene_delete`, `savingScene` toasts once the bridge reports it) |
| music | Spotify card from `buddy/spotify/state`, progress bar runs locally from `progress.at`; `watchSpotify()` POSTs `{"action":"watch"}` every 20 s while the dashboard is visible (spotify only polls Spotify while watched) |
| system | `renderSystem()`: one card per host from every `buddy/system(-<name>)?/state` (sorted by label; no fixed list: new hosts just appear), `meter()` bars for CPU/RAM/disk, throttle badges, card dims when that host's status isn't online. No chip in `SERVICE_CHIPS` (host names aren't fixed) |
| screener | `renderScreener()` (#80): one card from `buddy/screener/state` (section hidden until the service published once): the browse loop's state and mode, a likes meter, today's counts, the last action, the chats by state (#77, `screenerChats`), Stop/Pause/Resume on `buddy/screener/cmd` (Resume also ends a hold for Dev). Counts and kinds only, never names |
| services | `SERVICE_CHIPS`: fixed list of chips, each green when all its member services are `online`; beside the name the first member state that isn't `idle`, or the chip's own `detail()` (messenger: link trouble, else "2 open"; game (members game + table): the running game's status, "your move", "thinking", "paused"; screener: the loop's state unless idle, "needs you" on a hold) |
| riftbound | `#riftbound` (#53), see below |
| render | `render()` picks the page by `location.hash` (`PAGES`: dashboard / `#hue` / `#riftbound`; `page()`) |

`index.html` holds the static skeleton, including the forms: re-rendered inputs would lose typed text and focus.

## Riftbound page (`#riftbound`, 2.6)

Everything from `buddy/game/state` (public cards only, `services/game/CLAUDE.md` → Public state); commands to
`buddy/game/cmd`. Buddy's side on top, battlefields in the middle (units one compact line each), Dev's side below.
- `rbPlace(g)`: each option goes on a board object (`obj` shown on the board: badge with the count), else on a hand
  tile by `card` (every card Dev could hold, sorted by name: Buddy doesn't know Dev's hand), else the bar (pass,
  keep, done, optional costs). `rbAll`: every option as a list (fallback).
- The card sheet (`<dialog id="rb-sheet">`, `rbOpen({id}|{code}|{pile, who})`): card text (`rbText`: `:rb_…:` →
  symbols), the object's state, its moves; rebuilt on updates only when its sig changes.
- `rbPrompt(g)`: Dev names cards that turned public; tap a candidate per copy, Confirm at `n`; runes as whole
  combinations (`rbCombos`, ≤ 6). Selection in `rbPick`, reset when the prompt changes.
- `rbSend(cmd)` adds the shown `v`; `rbPending` blocks the controls until the payload's `v` moves on. **Refusals:**
  game doesn't answer them on the bus; an accepted command always brings a new payload at once, so none within 4 s
  = refused → toast.
- Paused: Resume only when `pause.resumable`; Concede and the "Buddy speaks" switch (`voice {on}`, off by default)
  while a game runs; the new-game form (static, `index.html`, with the voice checkbox) only without a running game.
  `v` goes only on `act`/`reveal` (the host refuses unknown fields).

## Adding something

- **New service on the panel:** a chip in `SERVICE_CHIPS`; a card reads `state["buddy/<svc>/state"]` and sends with
  `send("<svc>", {...})`. The command topic must exist in `topics.json` (and not be `"web": false`).
- **Anything that moves (motion) needs a login first** (D19). Until then `"web": false` keeps it off.
- **New command field from the panel:** prefer ids (`id`) over spoken names; the skill must accept it.

## Gotchas

- Each open `/api/events` holds one httplib worker thread; `StateStore::close()` ends them on shutdown.
- A stream gets a patch only if it already has the version the patch applies to (else the whole payload); the page
  must apply frames in order and reconnect on any mismatch.
- Service status chips are a fixed list on purpose: stale retained statuses (removed services, test clients) never show.
- `mosquitto_pub -r` test payloads on `buddy/+/state` stay retained and show up on the panel: clear with `-r -n`.
