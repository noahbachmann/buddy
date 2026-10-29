# services/web – web panel (D19)

HTTP ⇄ MQTT bridge plus a static page. No skill logic: the page sends the same commands brain does, the skills answer
with their retained state. User-facing behaviour and troubleshooting: `docs/guides/01-setup-summary.md` → 1.14.

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
| services | `SERVICE_CHIPS`: fixed list of chips, each green when all its member services are `online` |
| render | `render()` picks the page by `location.hash` (dashboard / `#hue`) |

`index.html` holds the static skeleton, including the forms: re-rendered inputs would lose typed text and focus.

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
