# 5.6 live game: Dev plays Buddy with real cards (temporary)

Temporary guide: Dev's part of #53 (the phase's software "Done when"). Delete this file when the game is played and
the notes are in #53; then tick 5.6 in `CLAUDE.md` and close #53 and #46.

## Goal

One full game against Buddy on the Riftbound page, with your real cards, Buddy's side on the sim table under stress
faults. Notes on pacing, prompts and layout go into #53.

## Prerequisites

- `buddy@game`, `buddy@table`, `buddy@web` running (`deploy/status.sh`; the panel's **game** chip is green).
- Your deck: Vi (Unleashed) or Zed (Vendetta), the one Buddy doesn't play; shuffled, plus its 12 runes and 3
  battlefields.
- A phone with the panel: `http://buddy.local:8080/#riftbound` (Android: `http://192.168.1.72:8080/#riftbound`).
- ~30 min. A game is ~16 turns; Buddy's turns take 5–20 s (2 s per decision, several decisions per turn).

## Steps

1. **Stress faults on the sim table** (scan errors, a disturbed pile: what the real table will see):

   ```bash
   ssh buddy micro ~/buddy/config/buddy.env      # add: BUDDY_TABLE_SIM_FAULTS=stress
   ssh buddy systemctl --user restart buddy@table
   ```

   For Buddy's lines on the Jabra (plays, attacks, scores, pauses), tick *Buddy speaks* in step 2 (off by default;
   the switch next to Concede changes it during the game).

2. **New game** on the page (bottom): *Buddy plays* = the other deck, *You play* = **your real cards**, *Buddy
   speaks* as you like → Start.

3. **Setup:** put one of your battlefields down, then answer *Which battlefield did you put down?* (tap it,
   Confirm). Draw 4, mulligan on the page (*Set aside a card* per card you put back, then *Keep*); put the set-aside
   cards on the bottom yourself and draw replacements.

4. **Play as usual, the page is the referee:**
   - **Your move:** cards with a number have moves on them: tap → card text and the moves. Cards you can play from
     your hand are the tiles under *From your hand* (every card you *could* hold: Buddy doesn't know your hand, tap
     the one you hold). End turn / pass / done are the big buttons. *All moves as a list* is the fallback.
   - **Name your card:** when a card of yours turns public without you choosing it (runes you channel, cards you
     burn, a discard you didn't pick), the page asks which one. Runes come as combinations ("1 × Fury + 1 × Order"):
     one tap, Confirm.
   - Move your physical cards the way the page says; Buddy's cards stay on the sim table (only the board shows them).
   - **Hey Jarvis:** say "Hey Jarvis" 3–4 times while the page shows *Buddy is thinking…*. Buddy must answer each
     time (#52 checked recorded clips under load, not the live mic).

5. **Write your notes into #53** (a comment):
   - Pacing: is 2 s per decision too slow? (Buddy's whole turn is what you wait for.)
   - Prompts: which ones were annoying, which card names were hard to find?
   - Layout on the phone: what did you scroll for, what was missing?
   - Anything the referee got wrong (a rule, a card): the turn and what happened; the replay is in
     `~/buddy/data/riftbound/games/` (newest `.txt`).

6. **Faults back off** (or keep them on for more games):

   ```bash
   ssh buddy micro ~/buddy/config/buddy.env      # remove BUDDY_TABLE_SIM_FAULTS
   ssh buddy systemctl --user restart buddy@table
   ```

## Verify

- The game ended (*You won!* / *Buddy won.*) without *This game can't go on*.
- `ssh buddy "~/buddy/bin/buddy-game --replay \$(ls -t ~/buddy/data/riftbound/games/*.txt | head -1)"` replays it
  without an error.
- Buddy reacted to every "Hey Jarvis" while thinking.

## Troubleshooting

| Symptom | What to do |
|---|---|
| *Buddy didn't take that: the game had moved on* | The board changed while you tapped (rare). Look again, tap again. |
| *My table isn't answering* (paused, Resume) | The table service hung or lost the broker; it picks up by itself once it's back. Else `deploy/logs.sh table`, then Resume. |
| *My table restarted and lost my cards* | The sim's piles are in memory: this game can't go on. Concede, start a new one. |
| Paused with *Resume* after a card problem | Stress faults rarely pause (never in 3000 soak games). Resume rescans the hand. |
| Page shows *The game service isn't running.* | `deploy/status.sh`, `deploy/logs.sh game`. A restart of `buddy@game` resumes the game. |
| A refused tap logged | `deploy/logs.sh game` shows `refused …` with the reason. |
