# Screener – Dev's to-dos

Temporary: delete this file when everything is ticked. Plan in #66. Your preferences and templates are already on the
Pi in `~/buddy/config/screener/` (host only, never in Git). This list covers what only you can do.

Commands run in a terminal on your PC (PowerShell works). `ssh buddy` logs you into the Pi.

## 1. Save your contact details on the Pi

The bot never sees these. Code inserts the one the match picks.

```bash
ssh buddy
echo '<your number, e.g. +41 79 123 45 67>' > ~/buddy/config/screener/number
echo '<your Instagram handle, e.g. @name>'   > ~/buddy/config/screener/instagram
chmod 600 ~/buddy/config/screener/number ~/buddy/config/screener/instagram
cat ~/buddy/config/screener/number ~/buddy/config/screener/instagram   # check: one line each
exit
```

- [x] `number` and `instagram` saved

## 2. Read through preferences and templates

Check that everything says what you mean, especially the German in `templates.md` (Claude translated it).

Read them (scroll with the arrow keys or space, `q` quits):

```bash
ssh -t buddy less ~/buddy/config/screener/preferences.md
ssh -t buddy less ~/buddy/config/screener/templates.md
```

Or print both at once:

```bash
ssh buddy cat ~/buddy/config/screener/preferences.md ~/buddy/config/screener/templates.md
```

To fix something yourself (`Ctrl+S` saves, `Ctrl+Q` quits):

```bash
ssh -t buddy micro ~/buddy/config/screener/preferences.md
ssh -t buddy micro ~/buddy/config/screener/templates.md
```

Or tell Claude what to change.

- [ ] `preferences.md` read and correct
- [ ] `templates.md` read, German checked

## 3. App profile

- [x] Add a short line to your app profile saying an AI assistant asks the first few questions for you (the ground
      rule in #66: the profile discloses it too, not only the first message).

## 4. Set up the phone (Huawei P20 Lite)

Factory reset first if it still has old data. Then:

- [x] Sign in with the Google account for the app, install **only** the app, and create the app profile by hand.
- [x] Play Store → the app → ⋮ → untick **Enable auto update**.
- [x] Settings → System → About phone → tap **Build number** 7× → Developer options unlocked.
- [x] Developer options: **USB debugging** on, **Allow ADB debugging in charge only mode** on (EMUI hides ADB
      without it), **Stay awake** (while charging) on, **Window / Transition / Animator animation scale** all **off**.
- [ ] Settings → Battery → **App launch** → the app → switch **Manage automatically** off and leave **Auto-launch**,
      **Secondary launch** and **Run in background** on. Why: EMUI kills apps in the background, and then the app's
      message notifications (how the bot notices new messages first) don't arrive.
- [x] Settings → Security & privacy → Screen lock → **None**.
- [x] Do Not Disturb on. Auto-rotate off (portrait). Leave font and display size at the default.
- [x] Plug the phone into the Pi by USB. On the phone, accept **Allow USB debugging** and tick **Always allow from this
      computer**.

## 5. Claude token for the screener (before the step 2 shadow session)

The screener's decisions run on your Claude plan through a long-lived token (not an API key). Create it on the Pi
with the Claude Code CLI the screener ships with; it prints a link: open it in your browser, log in, paste the code
back, and it prints the token (starts with `sk-ant-oat`):

```bash
ssh -t buddy '~/buddy/venv/lib/python3.13/site-packages/claude_agent_sdk/_bundled/claude setup-token'
```

Save it (paste the token, `Ctrl+S`, `Ctrl+Q`), then lock the file down and check it's one line:

```bash
ssh -t buddy micro ~/buddy/config/screener/claude-token
ssh buddy 'chmod 600 ~/buddy/config/screener/claude-token && wc -l ~/buddy/config/screener/claude-token'
```

- [x] `claude-token` saved

## 6. Before measure session 3

The bot adapts your *Like comments* (`templates.md`, one line each: `- id | de|en | fit hint | text`). Session 2's
likes found no fitting one 7 times (topics: shop pinboards, DIY building, a favourite bird, food opinions, nerdy
work, a favourite band, absurd hypotheticals). Add a few general ones for such hooks, or send Claude rough lines:

```bash
ssh -t buddy micro ~/buddy/config/screener/templates.md
```

- [ ] More like comments added
- [x] Active hours and daily like count decided (for step 3, #80): 8 likes, 10:00–18:00 (2026-11-23)

## Session 5: the dry loop (#80, from 2026-11-24)

The screener browses on its own 10:00–18:00 and skips what it would skip. When it would like a profile, or hits
something it can't handle, it stops and asks you on Telegram:

- [ ] A held like: like it on the phone (or tell Claude the row number to send it), or skip it, then tap **Resume**
  (Telegram or the panel's screener card). **Stop** ends the loop until you resume.
- [ ] Label the skipped profiles in batches when Claude sends their screenshots (≥ 50 profiles for the gate).

Step 0 is done (2026-11-16). The phone keeps its keyboard set to the screener's own one (no keys): to type by hand,
tap the keyboard icon at the bottom right and pick SwiftKey.
