# services/screener – the screener service (#66)

Operates a social app on a dedicated Android phone over adb (USB on the Pi). Plan, ground rules and steps: #66.
**Public repo:** never the app's name or what kind of app it is in code, fixtures, commits or docs; the package
comes from `BUDDY_SCREENER_APP` in the host's `buddy.env`. Personal data (dumps, screenshots) only in tmpfs or
`~/buddy/{config,data}/screener` on the Pi.

State: step 1 (service skeleton, device layer, screen model, navigator) and step 2 (decision layer + shadow mode).
The service itself acts on nothing; shadow mode runs as offline commands where Dev taps like/skip/send by hand. No
code path taps send like, skip or a chat send.

## Modules (`buddy_screener/`)

| File | What |
|---|---|
| `uitree.py` | `uiautomator` XML → `Node` tree (`desc`, `text`, `rid` without package, `Rect` bounds, flags) |
| `vocab.py` | Every app UI string the model relies on (English UI). The sanitizer keeps exactly these |
| `screens.py` | `classify()` → `Kind` (feed, sheet, likes, matches, chat, popup, other_app, unknown); readers `profile_view` (sections + their like buttons, details, floating obstacles, `at_top`), `sheet_view`, `match_rows`, `chat_view` (side left/right by position); `tappable()` = the allowlist |
| `profile.py` | `Capture`: one profile stitched across scroll dumps (offset per dump measured by voting on shared sections/texts, checked by `verify_shift`), `Section` refs `photo N` / `prompt N` in content y; `dhash` |
| `imaging.py` | Pillow: crop of a screenshot → dhash (photos all read "<name>'s photo" in the tree), → JPEG for the decision |
| `device.py` | adb: `dump` (retries), `screencap`, `tap` (jitter inside the middle half), `swipe`, `key`, `launch`, `foreground`, keyboard type/clear/read (`buddy.screener.ime` broadcasts), `ime_set`, `notifications`, `state`. `guard()` before every input action |
| `navigator.py` | Moves: `go_tab`, `capture_profile` (+ each detail row swiped to its end and flung back), `scroll_to(ref)`, `open_sheet` (+ scroll the panel clear), `type_comment` (read back), `back_out`, `open_chat`/`leave_chat`. `NeedsYou` keeps the raw dump in tmpfs |
| `sanitize.py` | Real dumps → fixtures: one mapping across a walk, names/prompts/texts → placeholders, package → `app.example`; refuses output with a word from `~/buddy/config/screener/forbidden` (host) |
| `library.py` | Host input: `preferences.md` (verbatim into the system prompt), `templates.md` section *Like comments* (`- id \| de\|en \| fit hint \| text`; broken line → whole file refused). Loaded per decision |
| `decide.py` | Agent SDK wrapper (`Decider.decide` = text check, then `browse` with photos): locked-down CLI options, text + browse schemas, `check_text`, `check_browse` (code re-checks: unknown section/comment → none, dealbreaker → skip, `fixes` kept), usage per call, `limit_until` pause, daily $ cap |
| `store.py` | SQLite `data/screener/screener.db`: `usage` (kept), `shadow` (bot decision + Dev's label as JSON, profile key only, 30 days) |
| `shadow.py` | Step 2 commands' logic: `run_shadow` (capture → decide → row; capture pickled to tmpfs without crops), `make_label`, `type_label`, `report` (done-when numbers) |
| `config.py` | `BUDDY_SCREENER_*` (incl. model, effort, decide timeout, daily $), kill switch files (`config/screener/stop`, `data/screener/paused`), token/limit/db paths, tmpfs dir |
| `main.py` | Service loop (phone check every 30 s, state on change, commands) + `--cmd screen\|dump\|fixture\|profile\|back-out\|walk\|templates\|shadow\|label\|type\|report` |

## Rules the code enforces

- Act only on a screen stable over two dumps; unknown screen → `NeedsYou`, never a blind tap.
- Promo popups (no tabs, a clickable "Close" button, nothing else known) are closed with that button only, by
  `wait_for`/`ensure_app`; more than `MAX_POPUPS` in a row → `NeedsYou`.
- Tap only what `tappable()` allows (tabs, back, match rows, like buttons, comment field); a target in the list must
  be fully inside it and clear of the floating skip/header buttons (`ProfileView.safe`).
- Text only through our keyboard, always read back.
- Kill switch: stop file, pause file, `systemctl stop`, unplug (adb error) all stop the next input action.

## Decisions (step 2)

- Agent SDK (`claude-agent-sdk`, its wheel bundles the CLI: aarch64 too, no Node). Auth: `claude setup-token`'s token
  in `config/screener/claude-token` → `CLAUDE_CODE_OAUTH_TOKEN`, CLI state in tmpfs (`CLAUDE_CONFIG_DIR`). An exported
  `ANTHROPIC_API_KEY` is refused (it would win and bill the API).
- Locked down per call: `tools=[]`, `setting_sources=[]`, `strict_mcp_config`, `skills=[]`, `--no-session-persistence`,
  empty tmpfs cwd, `verbatim_prompts` (else `@/path` in profile text is a file mention; with it the text arrives as written: checked).
- Structured output = the CLI's `--json-schema`: costs a second turn (`num_turns` 2), `max_turns` 3.
- Text first (`BUDDY_SCREENER_TEXT_FIRST`, on): a text-only call (`task` "text") returns stated dealbreakers; one →
  skip (`Browse.stage` "text"), photos never sent. Both stages use the same system prompt (the stage is the user
  message's first line), so the cached prefix serves both. `report` says whether it pays (early skips × a full call's
  mean $ vs all text checks).
- Fresh context per profile, not one chat per day: resent history grows quadratically, profiles anchor on each
  other, one injected text would stay for the rest of the day. The shared part (preferences + library) is the cached
  system prompt anyway.
- `RateLimitEvent.rate_limit_info.raw["unifiedWindows"]` has the plan's 5 h / 7 day utilization: logged per call.
- Photos: JPEG crops (long side `BUDDY_SCREENER_PHOTO_PX`, 512 ≈ 440 tokens; 768 was ≈ 980) taken with the hash
  screenshot, in `Section.jpeg`, never on disk. Photos are most of a profile's tokens.

## Shadow session (step 2, Dev taps)

`deploy/run.sh screener --cmd shadow` → prints `#id`, decision, reason, section + comment. Dev decides on the phone;
`--cmd label <id> like|skip [section comment]` (bare `like` = the bot's choice; `--wrong`, `--dealbreaker`,
`--sensitive`, `--note`). A comment Dev writes goes into `templates.md` first (id + fit hint from the section it was
used on), then into the label. `--cmd type <id>` types it into that section's panel and reads it back; Dev taps send
(or `--cmd back-out`). Only the newest row's capture stays in tmpfs. `--cmd report` = the gate.

### Claude's procedure for the shadow sessions (agreed with Dev, 2026-11-17)

Two kinds of session: first **tune** the preferences, then **measure** them frozen. A session runs until the day's
likes are used up. Dev taps on the phone and comments on decisions in chat (why like / skip).

Every profile, both kinds:

1. `--cmd shadow`, show Dev the bot's decision, reason, section + comment.
2. Dev decides and comments. Label it: `--note` = Dev's reason (condensed, no names); `--dealbreaker` if Dev calls
   it a hard no; `--wrong` if the bot's comment reads wrong on its section. Dev's own comment → a line in
   `templates.md` *Like comments* first (id, de|en, fit hint from the section, text verbatim), `--cmd templates` to
   check, then label with that id. The library grows live in both kinds.
3. Compare Dev's reason with the bot's reason and `preferences.md`. Sort the gap:
   - preferences silent on it → add;
   - preferences say something else → reword;
   - preferences already say it and the bot ignored it → a prompt/code issue (`decide.py`): note it for #66,
     don't patch preferences to compensate.
   Change preferences only for reasons that generalize (a pattern, or Dev states it as a rule). A one-off stays a
   note; unsure → ask Dev. Never a specific person's details in the preferences.

**Session 1, tune:** apply step 3's changes at once. Every change: copy the old file to
`config/screener/history/preferences-<UTC stamp>.md`, edit, show Dev the `diff -u` in chat (the next profile reads
the new file). No `report`: its numbers mix preference versions. End: summary of the changes, prompt/code issues
into #66 (neutral wording).

**Session 2, measure:** `preferences.md` frozen. Note the first shadow row id. Step 3's proposals go into
`data/screener/session-2.md` (host only), not into the preferences. Continue on the next days with the same frozen
file until ≥ 50 profiles (`GATE_PROFILES`). End: `report` on this session's rows only (needs a `--since <row id>` filter: not built yet, add before
session 2), evaluate, then show Dev the
proposed preference changes as a diff. Dev decides: good enough → automate (step 3), or another measure session
with the changes applied.

## Tests (`tests/`)

Fixtures are sanitized real dumps (step 1, 2026-11-16): `feed-01..11` one profile top to bottom (6 photos, 3
prompts), `sheet`/`sheet-typed` (photo panel), `sheet-low` (prompt panel under the skip button), `likes` (empty),
`matches`, `chat`, `popup` (feature ad on launch, 2026-11-17), `row-0..3` (another profile's detail row swiped
sideways to its end, 2026-11-17), `photo-caption` (a captioned photo: its button reads "Like photo prompt"),
`own-profile` (unknown). `fixture_files.py` loads them, `fake_phone.py` replays them as a phone (taps/swipes move between
dumps, records actions, `sent()` must stay empty). `test_decide.py` runs the decider against a fake SDK stream
(real `ResultMessage`/`RateLimitEvent` objects); `test_shadow.py` runs shadow → label → type on the fake phone. New fixture: `deploy/run.sh screener --cmd dump <name>` on the
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
- The details under prompt 1 start with a row that scrolls **sideways** (age, gender, height → location, children →
  family plans, zodiac): uiautomator lists only what's on screen, so the dealbreaker details hid off to the right
  until 2026-11-17. A 900 ms swipe moves it one screen; the end = an unchanged dump; a 150 ms fling right restores
  the exact start dump. Bounds are clipped to the list: a row touching the list's edge is cut, its y unusable at the
  top. Adds ~30 s per profile.
- A photo with a caption has a "Like photo prompt" button (a photo like, not a prompt); the caption text isn't in
  the tree.
- `type`/`scroll_to` track from where the capture ended (the profile's bottom): if Dev scrolled meanwhile, the first
  dump shares nothing with it → "lost track". Scroll back to the bottom, then retry (relocating: not built).
- `exec-out uiautomator dump /dev/tty` appends "UI hierchary dumped to: /dev/tty": `Device.dump` cuts it.
- Carried to later steps (not seen yet): an incoming like's card, an app notification's text, our own sent message's
  format in a chat. Each → a sanitized fixture when first seen.
