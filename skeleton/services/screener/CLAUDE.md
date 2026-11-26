# services/screener – the screener service (#66)

Operates a social app on a dedicated Android phone over adb (USB on the Pi). Plan and status: #66 (steps), #80
(browse loop), #77 (chats). **Public repo:** never the app's name or what kind of app it is in code, fixtures,
commits or docs, never a match's name; the package comes from `BUDDY_SCREENER_APP` in the host's `buddy.env`.
Personal data (dumps, screenshots, captures) only in tmpfs or `~/buddy/{config,data}/screener` on the Pi.

Modes (host `buddy.env`, all default `off`: a deploy never starts tapping): `BUDDY_SCREENER_BROWSE=dry|live` (the
browse loop), `BUDDY_SCREENER_CHAT=hold|live` (the chats). Shadow commands run offline (`--cmd …`). No code path taps a
paid option. A chat message is sent only by the chat driver, only after its read-back.

## Modules (`buddy_screener/`)

| File | What |
|---|---|
| `uitree.py` | `uiautomator` XML → `Node` tree (`desc`, `text`, `rid` without package, `Rect` bounds, flags) |
| `vocab.py` | Every app UI string the model relies on (English UI). The sanitizer keeps exactly these |
| `screens.py` | `classify()` → `Kind`; readers `profile_view` (sections + like buttons, details, obstacles, `at_top`, `at_bottom`; with a `name`: a chat's Profile tab), `sheet_view`, `match_rows`/`turn_headers`, `chat_view`, `unmatch_item`; `tappable()` = the allowlist, `actable()` = the acting taps |
| `profile.py` | `Capture`: one profile stitched across scroll dumps (offset per dump by voting on shared sections/texts, `verify_shift`; a blind gap past a video by the swipes' scale; feature-ad texts never details); `Section` refs `photo N` / `prompt N` / … in content y; `dhash` |
| `imaging.py` | Pillow: crop → dhash, crop → JPEG for the decision, `shot_pages` (half-size screenshots 4 to a page) |
| `device.py` | adb: `dump` (our reader in serve mode, else one-shot, else `uiautomator`), `screencap` (raw → RGB, `half`), jittered `tap`/`swipe`/`swipe_h`, `key`, `launch`, keyboard type/clear/read (`buddy.screener.ime`), `notifications`, `state`. `guard()` before every input |
| `navigator.py` | Moves: `go_tab`, `capture_profile(shots)` (detail rows swiped to their end and back; `shots` = dry pages on the way down; `stats` = `CaptureStats`), `scroll_to`, `open_sheet`, `type_comment`, `skip_profile`, `send_like` (via `_act`), `back_out`; chats: `matches()`, `open_chat`/`leave_chat`, `read_chat()`, `chat_profile()`, `type_message`/`send_message`/`clear_message`, `unmatch()`; `screenshots()` (`--cmd shots`). `NeedsYou` keeps the raw dump in tmpfs |
| `sanitize.py` | Real dumps → fixtures: names/texts → placeholders, package → `app.example`; refuses a word from the host's `forbidden` list |
| `library.py` | Host input, loaded per call: `preferences.md` (system prompt), `templates.md` (*Voice*, *Like comments*, chat templates as EN/DE blocks), `ChatRules` (`chat.md` + the chat's preference sections + questions + links), `Contacts` (`number`/`instagram` files, hidden from repr) |
| `decide.py` | Agent SDK wrapper: `Decider.decide` = photo check, then the full `browse` call; code re-checks the answer (`check_browse`); usage per call, `limit_until` pause, daily $ cap; `call(cache=False)` for the photo check |
| `shadow.py` | `judge` (capture → decide → row; capture pickled to tmpfs without crops), `make_label`/`act_label`, `report` (the gate's numbers, capture phases) |
| `likes.py` | The like budget from the action log: ≤ `BUDDY_SCREENER_DAILY_LIKES` (8) in any 24 h, each slot back 24 h after its like; `stuck` paywall → Dev |
| `browse.py` | The loop (#80): `Browser.step()` = one profile (window, budget, breaks → `judge` → skip / hold / like); dry pages in tmpfs `rows/<id>/` |
| `chat.py` | The chat as a pure state machine (`ChatFlow.step/conversed/verified/delivered/give_back`): disclosure first, manual detection, match checks, contact by template, refusals, timers, abuse guards, batching |
| `converse.py` | The chat's calls: `converse` (reply + state + answers + flags + contact + lang), `verify` (strict second opinion) |
| `chats.py` | The chats on the phone (#77): a pass over the matches list, links a match to our `liked` chat or an old match, runs `ChatFlow`; `hold` types and holds each message; give-backs; tmpfs review note per chat |
| `chatcmd.py` | `--cmd chats` (open chats + handovers, notes, tokens/$), `chat-edit <name> "<text>"`, `chat-back <name>` (a manual or handed-over chat back to the bot) |
| `hub.py` | Holds: `needs_you` file, the Telegram ask (Resume/Stop), Dev's answer |
| `store.py` | SQLite `data/screener/screener.db` (WAL): `usage` (kept), `shadow` (bot decision + Dev's label + `capture` timings, profile key only, 30 days), `actions` (every act and stop, no names, 30 days), `chats` (`chat.Chat` as JSON, never her texts; 30 days after it ended) |
| `sim.py`, `sim_cases.py`, `sim_personas.py` | `--sim`: scripted cases + persona chats on a fake clock, sentinel contacts, leak check, gate report |
| `config.py`, `main.py` | `BUDDY_SCREENER_*`, kill switch files; the service loop (one worker: a chat step, then a profile) + every `--cmd` |

## Rules the code enforces

- Act only on a screen stable over two dumps; unknown screen → `NeedsYou`, never a blind tap.
- Promo popups (no tabs, a "Close" button, nothing else known) closed with that button only, wherever a move
  waits for or expects a screen (they also come up mid-pass); > `MAX_POPUPS` in a row → Dev.
- Tap only what `tappable()` allows, fully inside the list and clear of the floating buttons (`ProfileView.safe`).
- Acting taps (skip, plain send like, the upsell's "Send Like anyway", chat send, Unmatch) only via `actable()`, only
  on the captured profile (name check), send only after the field reads back the text. Never the upsell's paid
  option or "Close sheet".
- Likes stop before the budget (`likes.py`); every act and stop goes into `actions`.
- Text only through our keyboard, always read back.
- Kill switch: stop file, pause file, `systemctl stop`, unplug. A hold (`data/screener/needs_you`) stops the loop's
  input, not Dev's `--cmd` (`hold_ok`).

## Decisions

- Agent SDK (its wheel bundles the CLI, aarch64 too). Auth: the plan token in `config/screener/claude-token` →
  `CLAUDE_CODE_OAUTH_TOKEN`, CLI state in tmpfs; an exported `ANTHROPIC_API_KEY` is refused (it would bill the API).
  Locked down per call: no tools, settings, MCP or skills, no session persistence, empty cwd, `verbatim_prompts`.
  Structured output = `--json-schema` (a second turn, `max_turns` 3). The plan's 5 h / 7 day use is logged per call.
- Cache: the CLI caches with the 1 h TTL (writes 2× input). The profile decision reads its system prompt back;
  the photo check never can (short prompt, new photos) and runs with `DISABLE_PROMPT_CACHING`. Every host-file
  edit rewrites the cached prompt.
- Fresh context per profile (no daily chat): no anchoring between profiles, the shared part is the cached prompt.
- Photo check (#83): a photos-only call before the full one (crops at `BUDDY_SCREENER_PHOTO_CHECK_PX`, 1024);
  overweight as Dev sees it, also mild, is a skip on its own ("physical preference"). The full call sees photos at
  `BUDDY_SCREENER_PHOTO_PX` (768: 512 missed). Crops live in memory only. Text-first check exists, off on the host.
- The browse answer: `soft_flags` (negatives) and `soft_plus` with evidence (`{flag, source, quote}`; code keeps a
  flag only when its quote is in the named section or the details), `uncovered` (things the preferences don't
  cover: input for the next preferences diff, never deciding). A like on a profile it called `generic` → skip.
- Like comments: written freely in Dev's voice (`templates.md` *Voice*; *Like comments* are examples, not a menu),
  facts about Dev only from *About Dev*. Language: German chat → Swiss German or standard German (never dialect
  when her German is standard), English chat → English, or German when she lists it. Checked by code (length, a
  copy of an example, language) then a check call (translation of an example, invented facts); one retry, else
  no comment.
- Fixed comments (`templates.md` *Fixed comments*, same line format, `de-ch` allowed): Dev's exact words for one
  topic (2026-11-26: LOTR). The model picks one by id when her section is about that topic; code sends its text
  verbatim: no copy check, no check call; only an English-only chat stops a German one.

## Sessions (Claude with Dev)

Measure sessions judge the frozen `preferences.md` against Dev's calls; the gate (`report`) is ≥ 85 % agreement over
≥ 50 profiles. Session 5 = the dry loop's rows from #176: `--cmd rows` lists the unlabelled ones with their pages;
Claude sends the pages in batches with **every detail written out** (the sideways row shows only its first screen),
labels per Dev (`--cmd label <id> like|skip|unjudged [section comment]`, `--text`, `--note`, `--dealbreaker`,
`--wrong`; `label` drops the pages), then `report --since 176`. Gaps between Dev's reasons and the bot's: preferences
silent → add, worded differently → reword, ignored → a prompt/code issue for #66. Proposals go into
`data/screener/session-<N>.md` (host only) while the preferences are frozen; after the session Dev decides on a
diff. Never a specific person's details in the preferences; host-file changes get a copy in `config/screener/history/`.

Shadow commands for a profile on screen: `--cmd shadow` (capture + decide + row), `type <id>` (types the labelled
comment and reads it back), `send <id>`, `skip <id>`, `back-out`; `--cmd shots` = screenshot pages for Dev away
from the phone.

## The browse loop (#80)

`Browser.step()`: off / stopped / paused / held → nothing; outside the active window → waiting; no like left in the
last 24 h → waits for the next slot; then `judge`. The window (#91): the first is today's `BUDDY_SCREENER_HOURS`; every
later one opens 24 h after the last like sent since the previous one opened (a held like answered late counts), at
any hour, for `BUDDY_SCREENER_WINDOW_H` (8); no like → 24 h after its start. Each opening is a `window` action
(start, end): the chain survives restarts. Dry: pages from the capture → `rows/<id>/`, skip →
skip, like → hold `would_like` (Dev likes or skips, then resumes). Live: skip, or `scroll_to` → `open_sheet` →
`type_comment` → `send_like`. A gap of 20–90 s, a 5–20 min break every 6–12 profiles. Holds wait for Dev (no
automatic retry); usage limit or daily $ cap → `limit` until it ends.

Screen (#90): the phone stays awake while charging (`stay_on_while_plugged_in`), so it never goes dark by itself.
The worker wakes it only for work (a profile, a due chat pass, a resume) and `Browser.run` turns it off once
(`Navigator.sleep`, KEYCODE_SLEEP, unguarded) when the next wait is ≥ 2 min or it holds for Dev; never while paused, nor while a like waits for Dev (off
~13 min, the app swapped the held profile for the next one: 2026-11-26).
Dev's phone commands (`_dev_navigator`) wake it first. Chat passes: every 10 min + a rising matches badge within the
window, every 30 min outside it (`BUDDY_SCREENER_CHAT_OFF_HOURS_MIN`).

Likes come back 24 h after each like, at most 8 in any 24 h (measured 2026-11-25): no reset at midnight. Hence the
window follows the likes (#91).

## Chat flow (#77)

Host: `chat.md` (screening questions, match rule, handover, tone, languages), `templates.md` chat blocks
(`disclosure`, `contact_ask`, `contact_ask_again`, `contact_choice`, `contact_choice_again`, `contact_number`,
`contact_instagram`, `closing`, `opt_out`, `question`, `ai_explain`), `number`, `instagram`. The chat calls see `chat.md` + the
preference sections *About Dev*, *Looking for*, *Good fit*, *Soft negatives/positives*, *Dealbreakers*.

- Which chats: a match linked to one of our `liked` chats (name + the head shows the same kind and comment), or an
  old match where we sent only the like; names in `config/screener/chat-ignore` and anything else are never touched.
- Opening: her reply to our like comment gets an `opener` (About facts, no question), then the disclosure; greetings
  alone skip the opener. Then the flow waits for her answer to the introduction (none → closed after `CLOSE_DAYS`).
- She doubts it's really an AI or is confused by the introduction (the model's `ai_doubt`): Dev's `ai_explain`
  (how the software works, a way out), no question; the flow waits, her answer counts as the answer to the
  introduction (then her question answered + the first question). Once per chat; unanswered → closed silently.
- Screening questions are Dev's words: code picks the next one in `chat.md`'s order (a required one in its place, not
  first) and appends it verbatim through the `question` template; the model only says whether one comes now.
- Code, not the model: disclosure first; ours on screen not in the log → `manual`; media → handover; match =
  required answers + no flag + ≥ 3 turns + the verifier. Then the bot's answer to her last message + `contact_ask`
  (number only) or, with a host `instagram` file, `contact_choice`; yes / phone / both / either → the number,
  instagram → the handle, a question → answered once + the `_again` template, still neither or her no → handover.
  The call is told what code offers. Every generated text refused (→ handover) on length, digit runs, handles,
  unknown links, contact values; our sent contact messages are redacted in every transcript.
- Language: first message by the prompts' majority (tie → German), then her latest message (German → standard German,
  else English).
- `hold`: each message typed and held; Telegram asks; Resume sends what the field holds (Dev's edit adopted as his
  wording). Dev's message beyond the held one, or a cleared field → `manual` (Telegram "Bot continues / Keep it
  mine"). `chat-back` returns a manual or handed-over chat to the state it left (`back_to`), her messages after our
  last one answered. Review from afar: `--cmd chats` → Dev approves or rewords (`chat-edit`) → Resume.
- Calls: `opener`, `first`, `reply` (once she's quiet: batched until her message count holds 60 s, at most 3 min),
  `nudge` (2 days quiet), `verify`. ~6–9k input tokens. Guards: a burst > 8 messages or > 1200 chars, 20 calls or
  $0.75 per chat, $0.15 per call, a $2 / 24 h chat pool; unrelated work declined once, the second → handover.
- Stored: the chat's state, her answers in a few words, flags, our messages, timers, a decision log, each call's cost.
  Her texts never: every open reads the chat off the screen; the tmpfs review note is the only copy.
- `--sim` on the Pi (cases, `--personas N`): own $ cap; persona fit outcomes reported, never asserted.

## Tests (`tests/`)

Fixtures are sanitized real dumps: `feed-01..11` (one profile top to bottom), sheets (`sheet*`), `likes`,
`matches*` (`matches-offer`: a paid-tier pill over the list), `feed-loading` (tab bar only, after a long sleep), `chat*`, `chat-profile-01..18`, `chat-menu`, `popup`, `row-0..3` (a detail row swiped to its end),
`photo-caption`, `upsell`, `out-of-likes`, `own-profile`, videos (`feed-video*`), voice (`feed-voice-1..4`), friends'
cards (`feed-friend-1..5`), polls (`feed-poll*`, `sheet-option*`). `fake_phone.py` / `fake_chat_phone.py` replay them as
a phone (taps/swipes move between dumps, `sent()` must stay empty); `test_decide.py` runs the decider on a fake SDK
stream. New fixture: `--cmd dump <name>` on the Pi, `--cmd fixture <out dir> <dumps…>` (one walk per call), check the
texts, copy over.

## Gotchas (measured on the phone)

- Screen reads go through our reader (`android/screener-dump`): `uiautomator`'s XML without its idle wait (a playing
  video never idles). Serve mode: 0.02 s on a quiet screen, else until 500 ms quiet. Another reader force-stops it.
  Never run a script on the phone while the loop can act.
- A capture: ~15 slow swipes (600 px over 900 ms scroll ~615 px; > ~1000 px leave no common anchor), ~65 s with the
  dry pages; decide ~10 s. `report` splits it per phase.
- The profile's end: the feed's last screen leaves ~300 px empty below the content (`at_bottom`); every other dump is
  flush. The chat's Profile tab ends flush: there, two swipes that move nothing.
- The first detail row scrolls **sideways** and is lazy: details off to its right aren't in the tree at all, not
  even as invisible nodes. Each row is swiped to its end (an unchanged dump) and flung back to its exact start.
- Details are the only anchors between prompt 1 and photo 2: `verify_shift` checks texts too.
- Videos, voice prompts, friends' cards and polls are sections: listed to the bot, never liked (a poll's options are
  liked one by one: the heart opens the panel under the whole card, the picked option is the non-clickable row). A
  video's player and the friends' carousel swallow swipes that start on them: swipes start in the list's right
  margin and off anything sideways (`_clear_start`). A playing video's "Elapsed time" label is ignored.
- Feature ads ("Try it" cards) sit between sections: anchors, never details. Their "Try it" sits inside the text's
  height: a dump that cuts the card can show the text alone (the capture drops it once it saw the ad whole).
- The comment panel opens where the section was, often under the skip button: `open_sheet` scrolls it to 60 %.
- `back_out`: BACK hides the keyboard, the next leaves the app; the relaunch can show **another** profile.
- Out of likes, "send like" opens a paywall; closing it sometimes shows the next profile (the like lost): the budget
  stops before it.
- `scroll_to` trusts the capture's offset only if the screen is exactly its last dump; otherwise back to the top first.
- Chats: "Your turn" and a collapsed "Their turn" header; a chat is a reversed list headed by our like; her messages
  carry " Name: text. "; with text typed the voice button becomes "Send message". Row keys tolerate a few bits of
  thumbnail-hash drift. The screen is off between passes: wake it first (a dark screenshot keys every row the same).
  The matches badge can stay up (unread chats the bot doesn't touch): only a badge above the one a pass left is news.
- A paid-tier offer can float over the matches list's bottom (2026-11-26): a clickable pill ("N% off …") with a
  countdown that ticks every second. Not a row (`offers()`), clocks are out of `_signature` (else the list never
  stops scrolling), and list taps on Matches hit only `clear_part()`: above the tab bar, beside the pill.
- Woken after ~30 min off, the feed can show the tab bar alone while it reloads (`feed-loading`): `ensure_app` waits
  up to `loading_s` (20 s) for a known screen before holding.
- Not seen yet (each → a sanitized fixture when first seen): an incoming like's card, an app notification's text, a
  new match without messages, the unmatch confirmation, a photo or voice note from her.
