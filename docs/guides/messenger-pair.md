# Messenger pairing (#68): temporary, delete when done

Goal: Buddy can write to your phone and ask you things. About 5 minutes. Claude's part is done: the service is on
the Pi and runs as "not paired". What it does: `docs/guides/01-setup-summary.md` → 1.16.

## 1. Create Buddy's bot (Telegram, phone or PC)

1. Open a chat with **@BotFather** (the one with the blue check mark) and send `/newbot`.
2. Name: `Buddy`. Username: anything free that ends in `bot`, e.g. `buddy_desk_<some letters>_bot`. The username is
   public: anyone can find the bot and write to it, Buddy ignores everyone but you.
3. BotFather answers with the **token** (`123456789:AA…`). It is the key to the bot: paste it only in step 2. Not
   into `buddy.env`, not into Git, not to Claude.
4. Send `/setjoingroups`, pick the bot, **Disable**: nobody can put it into a group.

## 2. Pair (PC, from `skeleton/`)

```bash
deploy/run.sh messenger --pair
```

Paste the token (it isn't shown) and press Enter. Open the printed `https://t.me/…` link on your phone and tap
**Start** (or send the bot the 6 digits). The terminal says `✓ paired`, the bot writes "Buddy here ✓". Then:

```bash
ssh buddy systemctl --user restart buddy@messenger
```

The panel's `messenger` chip loses its "not paired".

## 3. Tell Claude "paired"

Claude then runs the checks with you (a test notice, a test question you answer on the phone, a restart between
question and answer, an offline alert) and deletes this guide.

| Symptom | Fix |
|---|---|
| "Telegram doesn't know this token" | Copy the whole token again (BotFather: `/mybots` → the bot → API Token) |
| "no code within 5 minutes" | Run `--pair` again, open the new link |
| Token ended up somewhere it shouldn't | BotFather: `/revoke`, then step 2 with the new token |
