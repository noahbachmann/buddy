# services/screener – the screener service (#66)

Operates a social app on a dedicated Android phone over adb (USB on the Pi). Plan, ground rules and steps: #66.
**Public repo:** never the app's name or what kind of app it is in code, fixtures, commits or docs; the package
comes from `BUDDY_SCREENER_APP` in the host's `buddy.env`. Personal data (dumps, screenshots) only in tmpfs or
`~/buddy/{config,data}/screener` on the Pi.

Step 1 state: service skeleton (state, `stop|pause|resume`), device layer, screen model, navigator. Nothing acts by
itself yet and nothing sends: no send like, skip or chat message exists in the code.

## Modules (`buddy_screener/`)

| File | What |
|---|---|
| `uitree.py` | `uiautomator` XML → `Node` tree (`desc`, `text`, `rid` without package, `Rect` bounds, flags) |
| `vocab.py` | Every app UI string the model relies on (English UI). The sanitizer keeps exactly these |
| `screens.py` | `classify()` → `Kind` (feed, sheet, likes, matches, chat, other_app, unknown); readers `profile_view` (sections + their like buttons, details, floating obstacles, `at_top`), `sheet_view`, `match_rows`, `chat_view` (side left/right by position); `tappable()` = the allowlist |
| `profile.py` | `Capture`: one profile stitched across scroll dumps (offset per dump measured by voting on shared sections/texts, checked by `verify_shift`), `Section` refs `photo N` / `prompt N` in content y; `dhash` |
| `imaging.py` | Pillow: crop of a screenshot → dhash (photos all read "<name>'s photo" in the tree) |
| `device.py` | adb: `dump` (retries), `screencap`, `tap` (jitter inside the middle half), `swipe`, `key`, `launch`, `foreground`, keyboard type/clear/read (`buddy.screener.ime` broadcasts), `ime_set`, `notifications`, `state`. `guard()` before every input action |
| `navigator.py` | Moves: `go_tab`, `capture_profile`, `scroll_to(ref)`, `open_sheet` (+ scroll the panel clear), `type_comment` (read back), `back_out`, `open_chat`/`leave_chat`. `NeedsYou` keeps the raw dump in tmpfs |
| `sanitize.py` | Real dumps → fixtures: one mapping across a walk, names/prompts/texts → placeholders, package → `app.example`; refuses output with a word from `~/buddy/config/screener/forbidden` (host) |
| `config.py` | `BUDDY_SCREENER_*`, kill switch files (`config/screener/stop`, `data/screener/paused`), tmpfs dir |
| `main.py` | Service loop (phone check every 30 s, state on change, commands) + `--cmd screen\|dump\|fixture\|profile\|back-out\|walk` |

## Rules the code enforces

- Act only on a screen stable over two dumps; unknown screen → `NeedsYou`, never a blind tap.
- Tap only what `tappable()` allows (tabs, back, match rows, like buttons, comment field); a target in the list must
  be fully inside it and clear of the floating skip/header buttons (`ProfileView.safe`).
- Text only through our keyboard, always read back.
- Kill switch: stop file, pause file, `systemctl stop`, unplug (adb error) all stop the next input action.

## Tests (`tests/`)

Fixtures are sanitized real dumps (step 1, 2026-11-16): `feed-01..11` one profile top to bottom (6 photos, 3
prompts), `sheet`/`sheet-typed` (photo panel), `sheet-low` (prompt panel under the skip button), `likes` (empty),
`matches`, `chat`, `own-profile` (unknown). `fixture_files.py` loads them, `fake_phone.py` replays them as a phone (taps/swipes move between
dumps, records actions, `sent()` must stay empty). New fixture: `deploy/run.sh screener --cmd dump <name>` on the
Pi, then `--cmd fixture <out dir> <dumps…>` (all dumps of one walk in one call), check the texts, copy over.

## Gotchas (measured on the phone, 2026-11-16)

- `uiautomator dump` takes ~2 s; a stable screen = 2 dumps. A profile capture: ~15 swipes, 2–3 min with photo hashes.
- Slow swipes (600 px over 900 ms) scroll 611–624 px; the first one on a profile also collapses the header. Swipes
  > ~1000 px left consecutive dumps without a common anchor: `MAX_MOVE` 700.
- Details (age, job, …) are the only anchors between prompt 1 and photo 2: `verify_shift` checks texts too, else a
  photo lines up with the wrong photo.
- The selected tab's wrapper is `selected`, the others `clickable`; "Matches, N new message" counts chats where it's
  our turn.
- The comment panel opens where the section was: often low, under the floating skip button. `open_sheet` scrolls it
  to 60 % of the list first.
- `back_out`: BACK hides the keyboard, the next leaves the app, BACK never closes the panel; relaunch drops it. The
  relaunch can show **another** profile (seen once after the profile had been open ~30 min): neither liked nor
  skipped (the likes banner stayed at "Step 1 of 5").
- `exec-out uiautomator dump /dev/tty` appends "UI hierchary dumped to: /dev/tty": `Device.dump` cuts it.
- Carried to later steps (not seen yet): an incoming like's card, an app notification's text, our own sent message's
  format in a chat. Each → a sanitized fixture when first seen.
