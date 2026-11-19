# services/screener – the screener service (#66)

Operates a social app on a dedicated Android phone over adb (USB on the Pi). Plan, ground rules and steps: #66.
**Public repo:** never the app's name or what kind of app it is in code, fixtures, commits or docs; the package
comes from `BUDDY_SCREENER_APP` in the host's `buddy.env`. Personal data (dumps, screenshots) only in tmpfs or
`~/buddy/{config,data}/screener` on the Pi.

State: step 1 (service skeleton, device layer, screen model, navigator), step 2 (decision layer + shadow mode) and
step 4's offline part (#77: chat flow + `--sim`, gate passed 2026-11-19; live wiring waits for step 3).
The service itself acts on nothing; shadow mode runs as offline commands. Skip and send are taps only through
`--cmd skip|send <id>`, each carrying out Dev's label of that row (2026-11-17, Dev's call: the start of step 3). No
code path taps a paid option or a chat send.

## Modules (`buddy_screener/`)

| File | What |
|---|---|
| `uitree.py` | `uiautomator` XML → `Node` tree (`desc`, `text`, `rid` without package, `Rect` bounds, flags) |
| `vocab.py` | Every app UI string the model relies on (English UI). The sanitizer keeps exactly these |
| `screens.py` | `classify()` → `Kind` (feed, sheet, likes, matches, chat, popup, upsell, other_app, unknown); readers `profile_view` (sections + their like buttons, details, floating obstacles, `at_top`), `sheet_view`, `match_rows`, `chat_view` (side left/right by position); `tappable()` = the allowlist, `actable()` = the acting one |
| `profile.py` | `Capture`: one profile stitched across scroll dumps (offset per dump measured by voting on shared sections/texts, checked by `verify_shift`; a blind gap past a video by the swipes' scale), `Section` refs `photo N` / `prompt N` / `video N` in content y; `dhash` |
| `imaging.py` | Pillow: crop of a screenshot → dhash (photos all read "<name>'s photo" in the tree), → JPEG for the decision |
| `device.py` | adb: `dump` (our reader `android/screener-dump`, else `uiautomator dump` with retries and `NotIdle`), `screencap`, `tap` (jitter inside the middle half), `swipe`/`swipe_h` (jittered start, drift, speed; length only shorter), `key`, `launch`, `foreground`, keyboard type/clear/read (`buddy.screener.ime` broadcasts), `ime_set`, `notifications`, `state`. `guard()` before every input action |
| `navigator.py` | Moves: `go_tab`, `capture_profile` (+ each detail row swiped to its end and flung back), `scroll_to(ref)`, `open_sheet` (+ scroll the panel clear), `type_comment` (read back), `skip_profile`, `send_like` (via `_act`; keeps the upsell's raw dump as tmpfs `upsell-last.xml`), `back_out`, `open_chat`/`leave_chat`. `NeedsYou` keeps the raw dump in tmpfs |
| `sanitize.py` | Real dumps → fixtures: one mapping across a walk, names/prompts/texts → placeholders, package → `app.example`; refuses output with a word from `~/buddy/config/screener/forbidden` (host) |
| `library.py` | Host input: `preferences.md` (verbatim into the system prompt), `templates.md` section *Like comments* (`- id \| de\|en \| fit hint \| text`; broken line → whole file refused). Loaded per decision. Chat (#77): `parse_chat_templates` (EN/DE blocks, placeholders per template), `ChatRules` (`chat.md` + the chat's preference sections + questions + allowed links), `Contacts` (number/instagram files, hidden from repr) |
| `chat.py` | The chat as a pure state machine (`ChatFlow.step/conversed/verified/delivered`, one `Action` at a time): disclosure first, manual detection, code's match checks, contact by template only, generated-text refusals, language, nudge/close/unmatch timers, `ChatLimits` |
| `converse.py` | The chat's calls on `Decider.call`: `converse` (reply + state + answers + flags + contact + lang), `verify` (strict second opinion, profile + transcript), prompts with escaped `<msg>` elements, `check_converse`/`check_verify` |
| `sim.py`, `sim_cases.py`, `sim_personas.py` | `--sim`: driver loop on a fake clock, 21 scripted cases, 8 persona cards (a model plays her), sentinel contacts, prompt leak check, gate report, transcripts in tmpfs |
| `decide.py` | Agent SDK wrapper (`Decider.decide` = text check, then `browse` with photos): locked-down CLI options, text + browse schemas, `check_text`, `check_browse` (code re-checks: unknown section/comment → none, dealbreaker → skip, adapted comment refused → the template's text or none, `fixes` kept), usage per call, `limit_until` pause, daily $ cap |
| `store.py` | SQLite `data/screener/screener.db`: `usage` (kept; live and `sim-` tasks capped apart), `shadow` (bot decision + Dev's label as JSON, profile key only, 30 days), `chats` (`chat.Chat` as JSON, never her texts; 30 days after it ended) |
| `shadow.py` | Step 2 commands' logic: `run_shadow` (capture → decide → row; capture pickled to tmpfs without crops), `make_label`, `type_label`, `act_label` (skip/send on Dev's label), `report` (done-when numbers) |
| `config.py` | `BUDDY_SCREENER_*` (incl. model, effort, decide timeout, daily $), kill switch files (`config/screener/stop`, `data/screener/paused`), token/limit/db paths, tmpfs dir |
| `main.py` | Service loop (phone check every 30 s, state on change, commands) + `--cmd screen\|dump\|fixture\|profile\|back-out\|walk\|templates\|shadow\|label\|type\|report`, `--sim [--case NAME…\|all] [--personas N]` |

## Rules the code enforces

- Act only on a screen stable over two dumps; unknown screen → `NeedsYou`, never a blind tap.
- Promo popups (no tabs, a clickable "Close" button, nothing else known) are closed with that button only, by
  `wait_for`/`ensure_app`; more than `MAX_POPUPS` in a row → `NeedsYou`.
- Tap only what `tappable()` allows (tabs, back, match rows, like buttons, comment field); a target in the list must
  be fully inside it and clear of the floating skip/header buttons (`ProfileView.safe`).
- Acting taps (skip button, plain send-like button, the upsell's "Send Like anyway") only via `Navigator._act` on
  `actable()`, only from `--cmd skip|send` on Dev's label, only on the captured profile (name check), send only when
  the field reads back the labelled comment. Never "Send a Rose…" (paid) or "Close sheet" (unclear if it drops the like).
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
- Like comments are patterns (#78, Dev 2026-11-19): `templates.md` text = one example of how Dev says it; the bot
  returns `comment_text` = that pattern adapted to her section, in the chat's language. Code refuses it like a chat
  text (`chat.refusal`: empty, > `COMMENT_MAX`, digit runs, handles, links) → the template's own text if its
  language matches the chat, else no comment. A label stores the text that goes out (`text`); `type`/`send` use it.
- Text first is off on the host since 2026-11-19 (0 early skips in 47 profiles, a third of the spend); photos 768 px
  since then (the bot missed "somewhat overweight" twice at 512).
- Photos: JPEG crops (long side `BUDDY_SCREENER_PHOTO_PX`, 512 ≈ 440 tokens; 768 was ≈ 980) taken with the hash
  screenshot, in `Section.jpeg`, never on disk. Photos are most of a profile's tokens.

## Shadow session (step 2, Dev taps)

`deploy/run.sh screener --cmd shadow` → prints `#id`, decision, reason, section + comment. Dev decides on the phone;
`--cmd label <id> like|skip [section comment]` (bare `like` = the bot's choice with its adapted text; `--text "…"` = Dev's version of a library comment; `--wrong`, `--dealbreaker`,
`--sensitive`, `--note`). A comment Dev writes goes into `templates.md` first (id + fit hint from the section it was
used on), then into the label. `--cmd type <id>` types it into that section's panel and reads it back; `--cmd send
<id>` sends it (or Dev taps, or `--cmd back-out`); `--cmd skip <id>` skips a skip-labelled profile. After a sent
like the app offers a paid upgrade (upsell sheet): `send` takes "Send Like anyway". Only the newest row's capture stays in tmpfs. `--cmd report` = the gate.

### Claude's procedure for the shadow sessions (agreed with Dev, 2026-11-17)

Two kinds of session: first **tune** the preferences, then **measure** them frozen. A session runs until the day's
likes are used up. Dev taps on the phone and comments on decisions in chat (why like / skip).

Every profile, both kinds:

1. `--cmd shadow`, show Dev the bot's decision, reason, section + comment.
2. Dev decides and comments. Label it: `--note` = Dev's reason (condensed, no names); `--dealbreaker` if Dev calls
   it a hard no; `--wrong` if the bot's comment reads wrong on its section. Dev's own comment → a line in
   `templates.md` *Like comments* first (id, de|en, fit hint = the general pattern, not this profile (#78), text verbatim), `--cmd templates` to
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
file until ≥ 50 profiles (`GATE_PROFILES`). End: `report --since <first row id>` (this session's rows and their shadow calls only), evaluate, then show Dev the
proposed preference changes as a diff. Dev decides: good enough → automate (step 3), or another measure session
with the changes applied.

## Chat flow (step 4, #77)

Host: `chat.md` (copied from `preferences.md` 2026-11-19: *Screening questions*, *Match rule*, *Handover*, *Tone*,
*Languages*; Dev removes the duplicates from `preferences.md` after step 4), `templates.md` chat blocks (`disclosure`,
`contact_offer`, `contact_number`, `contact_instagram`, `closing`, `opt_out`, `question`), `number`, `instagram`. The chat calls
see `chat.md` + *About Dev*, *Looking for*, *Good fit*, *Soft negatives/positives*, *Dealbreakers* (not *Physical*).

- A driver runs `ChatFlow` (the sim now, the live loop after step 3): `step` → send / converse / verify / unmatch /
  notify / wait; the driver reports back (`delivered`, `conversed`, `verified`, `unmatched`).
- Code, not the model: disclosure first; our like comment seeded into the sent log; ours on screen not in the log →
  `manual` for good; media → handover; match = required answers + no flag + ≥ 3 turns + verifier; the held `reply`
  keeps screening when that fails; contact phone/both → number (Dev), instagram → handle, unclear once → ask again;
  every generated text refused (→ handover) on length, digit runs, handles, links not in `chat.md`, contact values.
- Screening questions are Dev's words (Dev, 2026-11-19: never rephrased): `chat.md` has each as `N. "English"` + an
  indented `DE: "German"` line (+ free note lines for the model). The model only returns `ask` (the number) and a short
  reaction; code appends the question verbatim in the chat's language through the `question` template ("Noah
  asks: …"). Only the model's part goes through the refusal checks.
- Language: first message by the prompts' majority (tie → German), then her latest message (German / Swiss German →
  standard German, never dialect; anything else → English). First message and nudge get the language stated.
- Our sent contact messages are redacted in every transcript (`[contact sent]`): no contact value reaches a prompt.
- `--sim` on the Pi: `deploy/run.sh screener --sim` (cases), `--sim --personas 8` (personas only), `--case all
  --personas N` (both). ~5 s per call; the 21 cases ≈ 55 calls ≈ 4 % of the 5 h window, 8 personas ≈ 9 %. Own cap
  `BUDDY_SCREENER_SIM_USD` (24 h, apart from the live cap). Persona outcomes on profile fit are reported, never
  asserted: the repo is public and fit is the host preferences' call.
- Findings fixed by the sim (2026-11-19): a fit line written about her for Dev and sent to her; dialect replies to a
  Swiss German profile; a nudge in the wrong language; the verifier without the profile (it flagged the bot's
  profile reference as invented); a held reply without a question (the chat stalled).

## Tests (`tests/`)

Fixtures are sanitized real dumps (step 1, 2026-11-16): `feed-01..11` one profile top to bottom (6 photos, 3
prompts), `sheet`/`sheet-typed` (photo panel), `sheet-low` (prompt panel under the skip button), `likes` (empty),
`matches`, `chat`, `popup` (feature ad on launch, 2026-11-17), `row-0..3` (another profile's detail row swiped
sideways to its end, 2026-11-17), `photo-caption` (a captioned photo: its button reads "Like photo prompt"), `upsell` (hand-built from a live
dump's node list: replace with a real dump when next seen),
`own-profile` (unknown), `feed-video` (a video between a photo and a prompt, 2026-11-17), `feed-video-prompt` (a
profile's top with a playing video prompt under photo 1, 2026-11-18), `feed-poll` (a poll's card, a feature ad, prompt 1, 2026-11-18), `out-of-likes` (the paywall after "send like"
once the day's free likes are gone, 2026-11-18). `fixture_files.py` loads them, `fake_phone.py` replays them as a phone (taps/swipes move between
dumps, records actions, `sent()` must stay empty). `test_decide.py` runs the decider against a fake SDK stream
(real `ResultMessage`/`RateLimitEvent` objects); `test_shadow.py` runs shadow → label → type on the fake phone. New fixture: `deploy/run.sh screener --cmd dump <name>` on the
Pi, then `--cmd fixture <out dir> <dumps…>` (all dumps of one walk in one call), check the texts, copy over.

## Gotchas (measured on the phone, 2026-11-16)

- Screen reads go through our own reader (`android/screener-dump`, 2026-11-18): same XML as `uiautomator dump`
  (byte-identical on a still screen), ~1.5 s instead of ~1.9 s, and it reads while a video plays (idle wait best
  effort, 2.5 s max). It must be installed on the phone; without it `Device.dump` falls back to uiautomator.
- A stable screen = 2 dumps. A profile capture: ~15 swipes, 2–3 min with photo hashes.
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
- `scroll_to` (type, send) trusts the capture's offset only if the screen is exactly its last dump; otherwise (Dev
  scrolled) it goes back to the profile's top (offset 0, certain) first: matching a far jump can line up wrongly
  (`Capture.add` accepted feed-05 as feed-10 in a test). Photo hash / prompt text still checked before any tap.
- A video is a section (`video N`: View "<name>'s video", button "Like video"; or View "Video prompt: <question>",
  text = the question, first seen right under photo 1, 2026-11-18): listed to the bot, never commented
  on or liked. Its player adds a native layer larger than the card that can swallow a swipe starting on it: the
  capture took that for the profile's end and stopped at the first video (2026-11-17). Swipes start in the list's
  right margin (`_margin_x`, beside the cards), and the end = two swipes in a row that move nothing.
- Free likes run out (seen after 2 sent likes on 2026-11-18): "send like" opens a paywall (`OUT_OF_LIKES`, its own
  kind: closing it isn't a popup on the way). `send_like` closes it (Close only) and stops: the comment stays in the
  panel, but the field loses focus and reads empty until tapped: `send_like` taps the field before the read-back. Not always: on 2026-11-19 closing it showed the next profile (the liked one gone, like not
  sent): `send_like` reports that instead of waiting for the panel.
- A poll is a section (`poll N`: a View "Prompt: <question>" without ". Answer:", its parent = the card, options
  "Option: …" below; keyed by the question only, options may be cut off): listed, never commented on. Unknown until
  2026-11-18: the bot never saw it and `scroll_to` lost track on its card. The app's feature ads ("Try it" cards)
  sit between sections: their texts are anchors but not details.
- A playing video's "Elapsed time: N seconds" label ticks every second: `_signature` ignores it, else no screen
  with a video counts as still (stable reads, the bottom check).
- Input is jittered like a person (Dev, 2026-11-18: avoid looking like a bot): taps inside the target's middle half,
  swipes (start ±15 px, drift ±6, speed 0.85–1.2×, length 92–100 %), waits (`human_pause`: 0.9–1.6×, 10 % of them
  +0.5–2 s). Tests inject `pause` and a seeded `rng`.
- Fallback only (no own reader): a video autoplays: while it's on screen uiautomator often never sees the screen idle ("could not get idle
  state"), the app has no setting to stop it (2026-11-17). With a video prompt under photo 1 not even the profile's
  top reads (2026-11-18). `Device.dump` gives up after 2 idle failures (`NotIdle`,
  ~25 s); the navigator swipes on past it (`MAX_BLIND`) and stitches the gap by the swipes' median measured scale
  (`Capture.add(blind=True)`: an anchor only refines it within `BLIND_TOLERANCE`). The video section itself and
  whatever the gap hid are lost or incomplete (never liked). A profile ending on a video: capture backs up until
  readable. Untested on the phone: whether a partly visible video already plays.
- `exec-out uiautomator dump /dev/tty` appends "UI hierchary dumped to: /dev/tty": `Device.dump` cuts it.
- Carried to later steps (not seen yet): an incoming like's card, an app notification's text, our own sent message's
  format in a chat. Each → a sanitized fixture when first seen.
