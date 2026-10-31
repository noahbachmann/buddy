# services/game – Riftbound engine quick reference

What the engine does **now** (state after #25) and where the next work plugs in. Why it is built this way:
`docs/plans/05-riftbound.md` → "5.2 design". Sub-issues of 5.2: #20. Rules text: `~/buddy/data/riftbound/core-rules-2026-10-09.txt`
on the VM (grep the rule number); full card text: `~/buddy/data/riftbound/cards.json` (VM and Pi, not in Git).

## Files (`riftbound/`, pure library, no MQTT)

| File | What |
|---|---|
| `card_db`, `code`, `deck` | Card data, printed codes, decklists (5.1). `Card::timing` = leading `[Action]`/`[Reaction]` (`printed_timing()`); `Card::rules` = its `CardRules` (nullptr = not implemented) |
| `state` | `GameState` value: objects (`ObjId` + `gen`, 124), zone id lists, pools, battlefields (`controller`, `contested`), chain (`ChainItem` with targets, cost, step counters, unit location `to`), `passes`, `showdown` (`Showdown`: focus, passes, attacker, damage assignment), tasks, decision, RNG, and the ability data: `triggered`, `effects`, `delayed`, `uses`, `links`. `move_to` (raw zone change, no events), `draw`, `channel`, `recycle`, `turn_state`, `chain_priority`, `lethal_damage`, `check_invariants` |
| `rules` | `CardRules` (the hooks a card implements), `AbilitySpec`, `TargetSpec`, `CostMod`, `Mod`; `Resolution` (what `resolve()` works with); targets (`candidates`, `still_valid`), `total_cost`, `payable`; `filters::` |
| `actions` | Game actions abilities see: `kill`/`kill_all` (Die → Died), `deal`, `heal`, `exhaust`, `ready`, `recall`, `move` (Moved events, contests the destination), `contest`, `stun`, `buff`, `discard`, `empower`; `emit` (queue triggers), `replaced` (replacement effects); layers `traits`/`might`/`has` (Assault/Shield while designated); `give`/`give_might`/`double_might`, `expire`, `prune`; `uses`, `link`/`linked`; `has_units_at` |
| `pay` | Rune payment of Energy/Power; the domain is the only choice (`Recycle` decision) |
| `engine` | `new_game`, `legal_actions`, `apply`, `concede`, `playable`, `describe`; the loop `run()`, `cleanup()`, `run_task()`, `finalize()`, `resolve_step()`, `place_triggers()`; showdowns and combat (`begin`, `pass_focus`, `score`, `establish_control`, damage assignment `assign_options`), Standard Move (`can_move`) |
| `random_game` | `play_random()`: uniform random seats, all checks after every action; vanilla or with rules; counts spells, chain plays, activations, triggers, choices, moves, showdowns, combats, scorings |
| `rng` | xoshiro256\*\*, own `below`/`shuffle` (portable replays) |

## The loop (`engine.cpp` → `run()`), in this order every iteration

1. `cleanup_due` and nothing resolving (321) → `cleanup()` (323 in rule order: win, combat designations, lethal damage via `kill_all`, the Ending/Combat Special Cleanup steps, `prune`, lost control, recalls, Contested; then in Neutral Open a staged showdown/combat begins, or a `Stage` task when several are; Showdown Open: a showdown becomes a combat, 323.13. Re-arms the flag if it changed something, 322).
2. A decision is open → stop.
3. An item `resolving` → `resolve_step()`: `CardRules::resolve` until `Done` (then off the chain, a spell to the trash, `Activated` event) or `Wait` (a decision is open).
4. A pending chain item → `finalize()` the oldest, a step machine in `ChainItem::step`: May (triggers) → Location (units: base or a controlled battlefield, 355.2) → Targets (one Choose per slot, forced picks automatic) → Optional costs → total cost (356) → Discard / RecycleTrash costs → runes (`Recycle`) + self costs → `finished()`: Targeted events, permanents enter (`enters_ready`) with a Played event, a unit at a battlefield contests it, `add` abilities resolve at once. A trigger without targets or payment leaves (403.4, 405.2).
5. `triggered` not empty → `place_triggers()`: turn player's first, each player orders their own (TriggerOrder).
6. Chain not empty: `passes == 2` → start resolving the newest; else Priority for `chain_priority()`.
7. A showdown with its window open → Priority for the player with focus (first passing focus on if a play's chain just closed, 346).
8. `tasks` (stack, back runs next) → `run_task()`: Mulligan, Awaken (resets `uses`, `deaths_this_turn`), Beginning, Hold (scoring step), Channel, Draw, Main, Ending (PhaseStart events), Expiration (Ending Special Cleanup incl. "this turn" expiry and stuns; again after the chain while anything went on it, 317.2.f; then the next turn); StandardMove, Stage; after all passed in a showdown: ShowdownEnd, or CombatDamage (`Assign` picks, damage dealt at once, Combat Special Cleanup), CombatResult (control, conquer), CombatEnd. The chain comes before tasks: a Beginning-phase trigger resolves before Hold, conquer triggers before CombatEnd.
9. No chain, Main phase, no showdown → Priority for the turn player (335). Otherwise stop.

Priority actions: `Play` (hand cards `playable()`: type, timing, `can_play`, enough targets, payable), `Activate` (obj = source on the board or legend zone, n = ability: controller, timing, `usable`, `per_turn`, targets, cost), `Move` (Neutral Open main phase only: one per destination with a unit that can go, n = battlefield or `to_base`; the units are a `Move` choice), then `EndTurn` (Neutral Open) or `Pass` (closed: chain priority; Showdown Open: pass focus). `Choose` decisions: `options` in a fixed order, picks ascending, auto-finished at `max`, `Done` from `min`; the answer goes to `GameState::answer` for whoever asked. `apply` accepts only what `legal_actions` offers and throws `IllegalAction` with the state untouched.

## Writing a card (5.3)

- One `CardRules` subclass, static instance, `Card::rules` points at it. Ability tables are `static constexpr` arrays
  (`AbilitySpec`, `TargetSpec`, `Cost`); build entries with a constexpr lambda when fields beyond the first are set
  (designated initializers that skip fields trip `-Wmissing-field-initializers`). Examples of every kind:
  `tests/test_abilities.cpp`.
- `resolve(Resolution& r)`: `r.ability() == card_effect` for the card itself. Targets via `r.target(slot)` (no_obj =
  mistargeted: skip its instructions and the linked ones), `r.self()` (null once the source left), a choice:
  `r.ask(…)` + `return r.wait(next)`, then `r.answer()` in that step. Remembered values: `r.var(i)`, the trigger's
  noted info: `r.item().event`.
- Triggers: `triggers()` pushes ability indexes for an `Event`; a card's own death arrives as `Die` while it's still
  on the board (Deathknell), others see `Died` afterwards. Delayed: `r.delay(ability, subject, window)` and
  `delayed_trigger()` / `replace(…, delayed)`. Reflexive: `r.reflexive(ability)`.
- Passives: `passive()` adds `Mod`s for any subject; called again until nothing new applies, so conditions may read
  `now`. Cost changes: `cost_mods()` (own: check `item.card == self`). "Can't be chosen": `untargetable()`.
- Board events: `Moved` (subject, `place` = origin; "when I move"), `Attack`/`Defend` (as combat starts),
  `CombatStart` (subject = the battlefield card), `Conquer`/`Hold` (subject = the battlefield card, `player` = who
  scored, Conquer's `amount` = excess combat damage), `Played` (`place` = where it entered: "plays a unit here").
  "This combat" effects: `Duration::Combat`. Moving effects: `move(s, ids, where, cause)` (the caller checks the
  destination). Location of a unit: `zone == Battlefield` + `where`; the combat's battlefield: `s.showdown->where`.

Known gaps (no deck card needs them; add when one does): designation triggers are placed turn player first, not
attacker first (466.2.d.1: differs only if the non-turn player attacks); units arriving mid-combat get their
designation but no Attack/Defend event; damage assignment ignores prevention (467.2.c.5) and units that can't be dealt
damage (467.2.c.10); no "win/lose a combat" events (468.3); a unit changing controller at a battlefield doesn't contest
(190.3.a.2); replacement order (372) and "may" replacements (371.2) are
not decisions (fixed order: delayed newest first, then static by id); group targets (355.11), split damage (355.14),
component discounts (356.4.c), Add abilities during payment (357.1.a: Gold tokens can be used before playing instead,
but `playable` doesn't count them), abilities on the chain as targets, cost kinds beyond discard / recycle from trash
/ exhaust / kill / disempower self (Burn, "discard a spell": #26), dependency loops across objects (478.1.c: depth
limit). Deflect's targeting cost is #26. For 5.3: a battlefield's triggers ("when you conquer here") are controlled
by its owner (`controller_of`), not by the player who conquered; Mageseeker Investigator's cost on moving several units
needs a hook on the Standard Move.

## Invariants (`check_invariants`, run after every random-game action)

Every object in exactly one zone list matching its zone/owner/controller/where; single-card zones; pools ≥ 0; no
finalized permanent on the chain; pending items never below finalized ones; at most one item resolving, never a pending
one; Choose picks in bounds and ascending; Priority only for the turn player (open) or `chain_priority()` (closed),
never with a pending or resolving item, queued triggers, or after both passed; in a showdown's open state only for
the player with focus and only while its window is open; no decision before a due cleanup (except inside a
resolution); something to decide while the game runs; combat designations exactly the combat's units (after a
cleanup), none outside combat; no controlled battlefield without its controller's units in an open state (323.5). `play_random` also checks a constant object
count and, at each turn's first open Main-phase priority, readied board and empty pools.

## Tests (`tests/`, one file per rules area)

- `test_cards.hpp`: cards in code (`card`, `unit`, `spell`), `simple_deck("A")`, `spell_deck("A")` (Normal, Action,
  Reaction spells), `plain_rules` + `ruled(deck)` (a deck that runs outside vanilla mode), `with_rules`.
- `test_abilities.cpp`: one test card per ability kind, shaped like the decks' cards (Bolt = Hidden Blade, Retreat =
  Tactical Retreat, Shells = Divining Shells, Lurker, Strike = Ruthless Strike, Sharkling, Twirler, Merchant,
  Empowerer/Shadow Legend = Empower/Zed legend, …); `ability_deck()` holds them all for random games.
- `tests/data/cards.json`: the decks' printings **without text** (only the leading timing keyword). No test can read
  real card text.
- Files: `test_setup` (110–117), `test_turn` (315–317, Burn Out, win), `test_pay`, `test_chain` (chain, priority,
  timing, cleanups), `test_abilities` (360–406, targeting, costs, layers, 750), `test_combat` (144, 188–190,
  341–348, 447–474: moves, showdowns, combat, damage assignment, scoring; own test cards for the keywords and board
  triggers), `test_random` (50 games each: spell decks and the real decks vanilla, most must end by scoring; 60
  ability-deck games in `test_abilities`).
- `buddy-game --random --games 1000 --seed 0 --deck decks/vi-unleashed.txt --deck decks/zed-vendetta.txt` on the
  ASan build (`build-test/services/game/buddy-game`); `deploy/run.sh game --random …` on the Pi (Release).
  After #25 on the VM (Release): vanilla 0.58 ms per game, 17 turns (scoring ends games; before #25 they ran ~180
  turns to burn out); ability decks ~7 activations, ~9 triggers, ~36 choices per game.

## Gotchas

- Hand-editing the state in a test (damage, buffs, `controller`) doesn't set `cleanup_due`: nothing happens until a
  game action causes a cleanup (resolving a spell does: 319.5). After setup moves set `s.cleanup_due = false`, or
  `check_invariants` reports "decision open before a due cleanup".
- `move_to` emits no events and runs no replacements: rules code uses the `actions.hpp` functions (kill, discard, …)
  so triggers see what happened.
- A `Pass` alone causes no cleanup; resolving or adding a chain item does.
- Chain items hold indexes, not pointers: `s.chain` reallocates when triggers or reflexive items are added, so
  re-fetch an item after anything that can add one (`Resolution::item()` finds the resolving one each call).
- Discarding as a cost or effect fires discard triggers (the test deck's Shadow Legend): tests that expect one chain
  item use `settle()`.
- Units put on a battlefield by hand in a test contest it in the next cleanup (323.10.a) unless their controller
  controls it: set `battlefields[b].controller` (test_combat's `holds()`), or expect a showdown.
- Standard Moves and plays to a battlefield need Neutral Open: in tests, a showdown started by an earlier move must
  close (both pass) before the next move is offered.
- 323.5 (lose battlefield control) runs only in an open state: a cleanup right after `Play` is closed.
- Vanilla mode reads stats and `Card::timing`, nothing else of the text: `rules_of()` is nullptr for every card;
  without vanilla every card needs rules (D21), `test::ruled()` gives test decks empty ones.
- Battlefields are not in any zone list (`Battlefield::card`); a base list is its controller's base, so "in another
  player's base" (323.6) can't be represented.
