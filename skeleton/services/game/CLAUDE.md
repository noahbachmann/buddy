# services/game – Riftbound engine quick reference

What the engine does **now** (state after #23) and where the next work plugs in. Why it is built this way:
`docs/plans/05-riftbound.md` → "5.2 design". Sub-issues of 5.2: #20. Rules text: `~/buddy/data/riftbound/core-rules-2026-10-09.txt`
on the VM (grep the rule number); full card text: `~/buddy/data/riftbound/cards.json` (VM and Pi, not in Git).

## Files (`riftbound/`, pure library, no MQTT)

| File | What |
|---|---|
| `card_db`, `code`, `deck` | Card data, printed codes, decklists (5.1). `Card::timing` = leading `[Action]`/`[Reaction]` (`printed_timing()`) |
| `state` | `GameState` value: objects (`ObjId` + `gen`, 124), zone id lists, pools, chain, `passes`, `showdown`, tasks, decision, RNG. `move_to` (only way to change zones; sets `cleanup_due` on board enter/leave), `draw` (Burn Out inside), `channel`, `recycle`, `turn_state`, `chain_priority`, `might`, `lethal_damage`, `check_invariants` |
| `pay` | Costs and the canonical rune payment; the domain is the only choice (`Recycle` decision) |
| `engine` | `new_game`, `legal_actions`, `apply`, `concede`, `playable`, `describe`; the loop `run()`, `cleanup()`, `run_task()`, `finalize()`, `resolve()` |
| `random_game` | `play_random()`: uniform random seats, all checks after every action; counts spells and plays onto a chain |
| `rng` | xoshiro256\*\*, own `below`/`shuffle` (portable replays) |

## The loop (`engine.cpp` → `run()`), in this order every iteration

1. `cleanup_due` → `cleanup()` (323 in rule order; re-arms the flag if it changed something, 322).
2. A decision is open → stop.
3. `tasks` (stack, back runs next) → `run_task()`: Mulligan, Awaken, Beginning, Channel, Draw, Main, Ending,
   Expiration (Ending Special Cleanup `cleanup(s, true)` + next turn).
4. A pending chain item → `finalize()` the oldest: choices (none yet), `printed_cost`, payment (may stop at
   `Recycle`), finalized. Units and gear leave the chain at once (337.2); spells stay.
5. Chain not empty: `passes == 2` → `resolve()` the newest (vanilla: no effect, owner's trash); else Priority for
   `chain_priority()` = newest item's controller + `passes` (no priority field).
6. No chain, Main phase, no showdown → Priority for the turn player (335). Otherwise stop.

Priority actions: `Play` (hand cards `playable()`: type, timing by `turn_state()`, payable), then `EndTurn` (open) or
`Pass` (closed). `apply` accepts only what `legal_actions` offers and throws `IllegalAction` with the state untouched.

## Where #24 plugs in (grep `#24` in `riftbound/`)

Scope: every ability *kind* of the rules (363–398, durations, layers 475–482 in full order, targeting, cost
modifiers, 750), generic and with test cards in code, whatever the decks use. The decks' full text is a checklist
that no shape they need is missing (costs like "Disempower me", Tactical Retreat's replacement), not a limit.
Not here: keywords neither deck uses and the copy layer's contents (D21; #26 does the decks' keywords).

- Phase-step effects: `run_task` Beginning, Main, Ending; "this turn" expiry in `cleanup(s, true)`; 317.2.f
  (repeat Expiration while items went through FEPR) in the Expiration task.
- Death triggers: `cleanup()` 323.4 kills via `move_to(Trash)`. Deathknell-style triggers need an event there.
- `finalize()`: 355 choices (targets, modes), 356 cost modifiers, optional/additional costs.
- `resolve()`: card effects (step machine: needs a step counter on `ChainItem` so a decision mid-resolution resumes).
- `might()`: printed + buffs only; layers (479) replace it. `lethal_damage()` uses it.
- Abilities on the chain: `ChainItem::card == no_obj` is reserved for them (`check_invariants` skips it already);
  `finalize()` and `resolve()` assume a card today. Add abilities resolve at once like permanents (337.2).
- Triggered chains don't pass focus (346.1): matters once #25 has showdowns.

## Invariants (`check_invariants`, run after every random-game action)

Every object in exactly one zone list matching its zone/owner/controller/where; single-card zones; pools ≥ 0; no
finalized permanent on the chain; pending items never below finalized ones; Priority only for the turn player (open)
or `chain_priority()` (closed), never with a pending item or after both passed; no decision before a due cleanup;
something to decide while the game runs. `play_random` also checks a constant object count and, at each turn's first
priority, readied board, empty pools and no leftover chain.

## Tests (`tests/`, one file per rules area)

- `test_cards.hpp`: cards in code (`card`, `unit`, `spell`), `simple_deck("A")`, `spell_deck("A")` (Normal, Action,
  Reaction spells). New ability kinds get test cards here, not from `cards.json`.
- `tests/data/cards.json`: the decks' printings **without text** (only the leading timing keyword). No test can read
  real card text.
- Files: `test_setup` (110–117), `test_turn` (315–317, Burn Out, win), `test_pay`, `test_chain` (chain, priority,
  timing, cleanups), `test_random` (50 games each: spell decks and the real decks vanilla; spells and reactions must
  occur).
- `buddy-game --random --games 1000 --seed 0 --deck decks/vi-unleashed.txt --deck decks/zed-vendetta.txt` on the
  ASan build (`build-test/services/game/buddy-game`); `deploy/run.sh game --random …` on the Pi (Release).
  After #23: 0 failures, ~182 turns, 3.1 ms per game on the Pi.

## Gotchas

- Hand-editing the state in a test (damage, buffs, `controller`) doesn't set `cleanup_due`: nothing happens until a
  game action causes a cleanup (resolving a spell does: 319.5). After setup moves set `s.cleanup_due = false`, or
  `check_invariants` reports "decision open before a due cleanup".
- A `Pass` alone causes no cleanup; resolving or adding a chain item does.
- `GameState::showdown` is only read (timing, 323.5); nothing starts showdowns or gives focus until #25. Setting it in
  a running game leaves nobody with priority.
- 323.5 (lose battlefield control) runs only in an open state: a cleanup right after `Play` is closed.
- Vanilla mode reads stats and `Card::timing`, nothing else of the text; without vanilla every card needs rules (D21).
- Battlefields are not in any zone list (`Battlefield::card`); a base list is its controller's base, so "in another
  player's base" (323.6) can't be represented.
