# Screener – Dev's to-dos before step 0

Temporary: delete this file when everything is ticked. Plan in #66. Your preferences and templates are already on the
Pi in `~/buddy/config/screener/` (host only, never in Git). This list covers what only you can do.

Commands run in a terminal on your PC (PowerShell works). `ssh dev@buddy.local` logs you into the Pi, same as the
`buddy` alias.

## 1. Save your contact details on the Pi

The bot never sees these. Code inserts the one the match picks.

```bash
ssh dev@buddy.local
echo '<your number, e.g. +41 79 123 45 67>' > ~/buddy/config/screener/number
echo '<your Instagram handle, e.g. @name>'   > ~/buddy/config/screener/instagram
chmod 600 ~/buddy/config/screener/number ~/buddy/config/screener/instagram
cat ~/buddy/config/screener/number ~/buddy/config/screener/instagram   # check: one line each
exit
```

- [ ] `number` and `instagram` saved

## 2. Read through preferences and templates

Check that everything says what you mean, especially the German in `templates.md` (Claude translated it).

Read them (scroll with the arrow keys or space, `q` quits):

```bash
ssh -t dev@buddy.local less ~/buddy/config/screener/preferences.md
ssh -t dev@buddy.local less ~/buddy/config/screener/templates.md
```

Or print both at once:

```bash
ssh dev@buddy.local cat ~/buddy/config/screener/preferences.md ~/buddy/config/screener/templates.md
```

To fix something yourself (`Ctrl+S` saves, `Ctrl+Q` quits):

```bash
ssh -t dev@buddy.local micro ~/buddy/config/screener/preferences.md
ssh -t dev@buddy.local micro ~/buddy/config/screener/templates.md
```

Or tell Claude what to change.

- [ ] `preferences.md` read and correct
- [ ] `templates.md` read, German checked

## 3. App profile

- [ ] Add a short line to your app profile saying an AI assistant asks the first few questions for you (the ground
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

Step 0 is done (2026-11-16). The phone keeps its keyboard set to the screener's own one (no keys): to type by hand,
tap the keyboard icon at the bottom right and pick SwiftKey.
