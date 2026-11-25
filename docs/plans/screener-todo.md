# Screener – Dev's to-dos

Temporary: delete this file when everything is ticked. Plan in #66. Your host files live on the Pi in
`~/buddy/config/screener/` (never in Git). Phone setup, contact files, token and app profile are done.

## 1. Read through preferences and templates

Check that everything says what you mean, especially the German lines in `templates.md` that Claude wrote (marked
"Dev to check": the contact asks, `contact_choice`, `contact_choice_again`, the Instagram note).

```bash
ssh -t buddy less ~/buddy/config/screener/preferences.md
ssh -t buddy less ~/buddy/config/screener/templates.md
```

To fix something yourself (`Ctrl+S` saves, `Ctrl+Q` quits), or tell Claude what to change:

```bash
ssh -t buddy micro ~/buddy/config/screener/preferences.md
ssh -t buddy micro ~/buddy/config/screener/templates.md
```

- [ ] `preferences.md` read and correct
- [ ] `templates.md` read, German checked

## 2. Session 5: the dry loop (#80)

Browsing is off until your likes are back (tell Claude to switch it back to `dry`). While it runs, it browses
10:00–18:00 and stops on Telegram when it would like a profile:

- [ ] A held like: like it on the phone (or tell Claude to send it), or skip it, then tap **Resume**.
- [ ] Label the skipped profiles in batches when Claude sends their screenshots (≥ 50 profiles for the gate).

The phone keeps the screener's own keyboard (no keys): to type by hand, tap the keyboard icon at the bottom right and
pick SwiftKey.
