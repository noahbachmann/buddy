# services/game – Riftbound engine quick reference

What the engine does **now** (state after #33: 5.3 done, every card of both decks) and where the next work plugs in.
Why it is built this way: `docs/plans/05-riftbound.md` → "5.2 design", "5.3 as built" (with the rulings for Dev).
Sub-issues: 5.2 #20, 5.3 #33. Rules text: `~/buddy/data/riftbound/core-rules-2026-10-09.txt`
on the VM (grep the rule number); full card text: `~/buddy/data/riftbound/cards.json` (VM and Pi, not in Git).

## Files (`riftbound/`, pure library, no MQTT)

| File | What |
|---|---|
| `card_db`, `code`, `deck` | Card data, printed codes, decklists (5.1). `Card::timing` = leading `[Action]`/`[Reaction]` (`printed_timing()`); `Card::rules` = its `CardRules` (nullptr = not implemented) |
| `state` | `GameState` value: objects (`ObjId` + `gen`, 124; `attached` = the unit an Equipment hangs on), zone id lists, pools, battlefields (`controller`, `contested`), chain (`ChainItem` with targets, cost, step counters, unit location `to`, `play` = `played::` bits: Flow, Ambush, token to base / exhausted; `rep` = Repeat execution), `passes`, `showdown` (`Showdown`: focus, passes, attacker, damage assignment), tasks, decision, RNG, and the ability data: `triggered`, `effects`, `delayed`, `uses`, `links`. `move_to` (raw zone change, no events; carries attached gear, detaches it when its unit leaves the board, a token off the board → `Removed`), `create` (new object), `draw`, `burn`, `channel`, `recycle`, `turn_state`, `chain_priority`, `lethal_damage`, `check_invariants` |
| `rules` | `CardRules` (the hooks a card implements), `AbilitySpec` (`builtin`: Empower, Equip), `TargetSpec`, `CostMod`, `Mod`; `Resolution` (what `resolve()` works with); `active_rules` (rules that work where the card is, none while attached); targets (`candidates`, `still_valid`, `target_slots`/`target_spec`: once per Repeat), `optional_costs` (card's, Accelerate, Repeats), `total_cost` (Flow base, Deflect), `payable`, `completable` (355.16: targets + cost can still work out); `predict`, `resolve_builtin` (Vision, Empower, Equip); `filters::` |
| `actions` | Game actions abilities see: `kill`/`kill_all` (Die → Died), `deal`, `heal`, `exhaust`, `ready`, `recall`, `move` (Moved events, contests the destination), `contest`, `stun`, `buff`, `discard`, `empower`/`disempower`, `banish` (Banished event), `return_to_hand`, `add`, `pay_energy`, `attach`/`detach`, `play_token`; `emit` (queue triggers, Vision), `replaced` (replacement effects); layers `traits`/`might`/`has` (Assault/Shield while designated, Might Bonus of attached gear); `mighty`; `give`/`give_might`/`double_might`, `expire`, `prune`; `uses`, `link`/`linked`; `has_units_at` |
| `tokens` | The decks' tokens with their rules: `bird()` (Deflect), `shadow_clone()` (attack trigger), `gold()` (Add [A]) |
| `pay` | Rune payment of Energy, domain Power, then any-domain Power ([A]); which rune to recycle is the only choice (`Recycle` decision, one pick per domain) |
| `engine` | `new_game`, `legal_actions`, `apply`, `concede`, `playable`, `describe`; the loop `run()`, `cleanup()`, `run_task()`, `finalize()`, `resolve_step()`, `place_triggers()`; showdowns and combat (`begin`, `pass_focus`, `score`, `establish_control`, damage assignment `assign_options`), Standard Move (`can_move`) |
| `random_game` | `play_random()`: uniform random seats, `GameChecks` after every action (invariants, card count, turn start); vanilla or with rules; counts spells, chain plays, activations, triggers, choices, moves, showdowns, combats, scorings; `log` = the actions (failing one included) |
| `hidden` | `view(s, p)`: objects p doesn't know (`Object::known` bit, set by `move_to`/`recycle`/`predict`) get `hidden_card()`, RNG reset; not playable. `determinize(view, seat0, seat1, rng)`: per owner the decklist minus visible cards, shuffled onto the hidden objects (rune deck: runes), RNG reseeded |
| `replay` | `Replay` (seed, vanilla, both decklists' text, actions) ⇄ text (`parse_replay`, `to_text`: annotated by replaying), `run_replay` (GameChecks after every action; `buddy-game --replay`) |
| `rng` | xoshiro256\*\*, own `below`/`shuffle` (portable replays) |
| `cards/` | The decks' cards (5.3, #33): `vi_unleashed.cpp`, `zed_vendetta.cpp` (one `CardRules` per card, its text above it, an `Entry` table at the end), `common.hpp` (`keywords({…})`, `cost()`, `discard()`, `count_named`, `battlefield_of`, `conquered_with`, `played_me`, `loot`, `destinations`, `Deathknell` base); `rules_for(name)`, attached by `CardDb::parse`: every loaded db (the test fixture too) has them |

## The loop (`engine.cpp` → `run()`), in this order every iteration

1. `cleanup_due` and nothing resolving (321) → `cleanup()` (323 in rule order: win, combat designations, lethal damage via `kill_all`, the Ending/Combat Special Cleanup steps, `prune`, lost control, recalls, Contested; then in Neutral Open a staged showdown/combat begins, or a `Stage` task when several are; Showdown Open: a showdown becomes a combat, 323.13. Re-arms the flag if it changed something, 322).
2. A decision is open → stop.
3. An item `resolving` → `resolve_step()`: `CardRules::resolve` (or `resolve_builtin`) until `Done` (again per paid Repeat with `rep` + 1; then off the chain, a spell to the trash or banished if played for Flow, `Activated` event) or `Wait` (a decision is open).
4. A pending chain item → `finalize()` the oldest, a step machine in `ChainItem::step`, in 355's order: May (triggers) → Optional costs (355.1.a: card's, Accelerate, Repeat) → Location (units: base or a controlled battlefield, + Ambush battlefields, 355.2; tokens "to your base" skip it) → Targets (one Choose per slot, once more per paid Repeat, forced picks automatic) → total cost (356) → Discard / RecycleTrash costs → runes (`Recycle`) + self costs (exhaust, disempower, burn, banish, kill) → `finished()`: Targeted events, permanents enter (`enters_ready` or Accelerate) with a Played event (→ Vision), a unit at a battlefield contests it, `add` abilities resolve at once. Optional costs and target picks are offered only if `completable()` (the rest can still be chosen and paid: Deflect, target-dependent costs). A trigger without targets or payment leaves (403.4, 405.2).
5. `triggered` not empty → `place_triggers()`: turn player's first, each player orders their own (TriggerOrder).
6. Chain not empty: `passes == 2` → start resolving the newest; else Priority for `chain_priority()`.
7. A showdown with its window open → Priority for the player with focus (first passing focus on if a play's chain just closed, 346).
8. `tasks` (stack, back runs next) → `run_task()`: Mulligan, Awaken (resets `uses`, `deaths_this_turn`), Beginning, Hold (scoring step), Channel, Draw, Main, Ending (PhaseStart events), Expiration (Ending Special Cleanup incl. "this turn" expiry and stuns; again after the chain while anything went on it, 317.2.f; then the next turn); StandardMove, Stage; after all passed in a showdown: ShowdownEnd, or CombatDamage (`Assign` picks, damage dealt at once, Combat Special Cleanup), CombatResult (control, conquer), CombatEnd. The chain comes before tasks: a Beginning-phase trigger resolves before Hold, conquer triggers before CombatEnd.
9. No chain, Main phase, no showdown → Priority for the turn player (335). Otherwise stop.

Priority actions: `Play` (hand cards, and trash cards with Flow, `playable()`: type, timing or Ambush (a Reaction to a battlefield where the player has units), `can_play`, `completable`), `Activate` (obj = source on the board or legend zone, not attached, n = ability: controller, timing, `usable`, `per_turn`, Empower only if not Empowered, `completable`), `Move` (Neutral Open main phase only: one per destination with a unit that can go, n = battlefield or `to_base`; the units are a `Move` choice), then `EndTurn` (Neutral Open) or `Pass` (closed: chain priority; Showdown Open: pass focus). `Choose` decisions: `options` in a fixed order, picks ascending, auto-finished at `max`, `Done` from `min`; the answer goes to `GameState::answer` for whoever asked. `apply` accepts only what `legal_actions` offers and throws `IllegalAction` with the state untouched.

## Hidden information (128)

`Object::known`: who knows which card it is. Public zone → everyone; into a hand → + owner; facedown → + controller;
into a deck or set aside → unchanged (a card seen going to the bottom stays known); `recycle` of 2+ → nobody (random
order); `predict` → + predictor. Setup creates deck objects after shuffling, so ids say nothing about cards. A card
rule that looks at or reveals a hidden card (5.3) must set `known` too; `check_invariants` checks public and hand
cards. `test_hidden.hpp`'s `views_along_a_game` checks after every action that a determinization is valid, has the
same view and gives the deciding player the same legal actions: run it on any new deck.

## Writing a card (5.3)

- One `CardRules` subclass, static instance, `Card::rules` points at it. Ability tables are `static constexpr` arrays
  (`AbilitySpec`, `TargetSpec`, `Cost`); build entries with a constexpr lambda when fields beyond the first are set
  (designated initializers that skip fields trip `-Wmissing-field-initializers`). Examples of every kind:
  `tests/test_abilities.cpp`.
- `resolve(Resolution& r)`: `r.ability() == card_effect` for the card itself. Targets via `r.target(slot)` (no_obj =
  mistargeted: skip its instructions and the linked ones), `r.self()` (null once the source left), a choice:
  `r.ask(…)` + `return r.wait(next)`, then `r.answer()` in that step. Remembered values: `r.var(i)`, the trigger's
  noted info: `r.item().event`.
- Keywords: `keywords()` is all Accelerate, Ambush, Assault, Deflect, Ganking, Shield, Tank and Vision need.
  Deathknell = keyword + a `Die` trigger. Empower / Equip: an `AbilitySpec` with `builtin` and the cost (Equip's
  target defaults to a friendly unit; "discard a spell": `Cost::discard_spell`). `[Empowered]` text = a passive or
  trigger checking `Object::empowered`. Repeat: `repeat_costs()` (the card's slots repeat, `r.target(k)` maps to
  the running execution). Flow: `flow_cost()`. Equipment: `might_bonus()` and `effect_keywords()` (not in Riftcodex: from the printed
  card; the keywords join the unit's own in `traits()`). "Move a unit" without a destination: `cards::destinations()`
  as an `r.ask` choice.
  Cost kinds: `Cost` fields (`any_power` = [A], `burn`, `banish_self`, …).
- Tokens: `play_token(s, tokens::bird(), p, played::to_base)` from a resolution; it finalizes after it.
- Battlefields (190.6): `controller_of` = the battlefield's controller, else the turn player; an ability that names
  the acting player ("they may pay", "when you conquer here") sets `AbilitySpec::event_player`: the event's player
  controls it. Legends ready in Awaken like the board (315.1.b).
- Triggers: `triggers()` pushes ability indexes for an `Event`; a card's own death arrives as `Die` while it's still
  on the board (Deathknell), others see `Died` afterwards. Delayed: `r.delay(ability, subject, window)` and
  `delayed_trigger()` / `replace(…, delayed)`. Reflexive: `r.reflexive(ability)`.
- Passives: `passive()` adds `Mod`s for any subject; called again until nothing new applies, so conditions may read
  `now`. Cost changes: `cost_mods()` (own: check `item.card == self`). "Can't be chosen": `untargetable()`.
- Board events: `Banished` (`player` = who banished, `place` = origin: Zed's legend), `Moved` (subject, `place` = origin; "when I move"), `Attack`/`Defend` (as combat starts),
  `CombatStart` (subject = the battlefield card), `Played` of a permanent: `amount` = its `paid` bits ("if you
  paid the additional cost"), `Conquer`/`Hold` (subject = the battlefield card, `player` = who
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
but `playable` doesn't count them), abilities on the chain as targets, dependency loops across objects (478.1.c: depth
limit); `completable` fills "up to N" slots at their minimum and multi-target slots with the first candidates;
Ambush's location isn't re-checked if the units leave before finalizing (822.3); Equipment effect text beyond
keywords (`effect_keywords()`, 718.3), reveal (425), counter (426), Equipped events (818.2.a); `pay_energy` pays
Energy only. The Standard Move's tax (`CardRules::move_tax`, Mageseeker) caps the Move pick count at what's payable
and recycles the runes in pay.hpp's order: no Recycle decision outside the chain ⚠️.

## Invariants (`check_invariants`, run after every random-game action)

Every object in exactly one zone list matching its zone/owner/controller/where; public cards known to everyone, hand
cards to their owner; single-card zones; pools ≥ 0; no
finalized permanent on the chain; pending items never below finalized ones; at most one item resolving, never a pending
one; Choose picks in bounds and ascending; Priority only for the turn player (open) or `chain_priority()` (closed),
never with a pending or resolving item, queued triggers, or after both passed; in a showdown's open state only for
the player with focus and only while its window is open; no decision before a due cleanup (except inside a
resolution); something to decide while the game runs; combat designations exactly the combat's units (after a
cleanup), none outside combat; no controlled battlefield without its controller's units in an open state (323.5);
attached gear with its unit on the board. `GameChecks` (play_random, run_replay) also checks a constant card count
(tokens come and go) and, at each turn's first open Main-phase priority, readied board and empty pools (a mid-turn
start, e.g. a determinization, skips its own turn).

## Tests (`tests/`, one file per rules area)

- `test_cards.hpp`: cards in code (`card`, `unit`, `spell`), `simple_deck("A")`, `spell_deck("A")` (Normal, Action,
  Reaction spells), `plain_rules` + `ruled(deck)` (a deck that runs outside vanilla mode), `with_rules`.
- `test_play.hpp`: `make`, `Keyworded`, `deck_of`, `started(deck)`, `put`, `offered`, `play`/`pass`/`pick`/`done`,
  `settle`, `expect_ok` for new test files (the older ones keep their own copies).
- `test_abilities.cpp`: one test card per ability kind, shaped like the decks' cards (Bolt = Hidden Blade, Retreat =
  Tactical Retreat, Shells = Divining Shells, Lurker, Strike = Ruthless Strike, Sharkling, Twirler, Merchant,
  Empowerer/Shadow Legend = Empower/Zed legend, …); `ability_deck()` holds them all for random games.
- `tests/data/cards.json`: the decks' printings **without text** (only the leading timing keyword). No test can read
  real card text.
- Files: `test_setup` (110–117), `test_turn` (315–317, Burn Out, win), `test_pay`, `test_chain` (chain, priority,
  timing, cleanups), `test_abilities` (360–406, targeting, costs, layers, 750), `test_combat` (144, 188–190,
  341–348, 447–474: moves, showdowns, combat, damage assignment, scoring; own test cards for the keywords and board
  triggers), `test_actions` (408–446, 701–711, Burn / discard-a-spell / banish-me costs), `test_keywords` (Deflect,
  Vision, Repeat, Ambush, Empower, Flow, Equip and attachment, tokens; 40 random games each for both files' decks),
  `test_random` (50 games each: spell decks and the real decks vanilla, most must end by scoring; 60
  ability-deck games in `test_abilities`), `test_hidden` (knowledge, view, determinize; views along random games, the
  ability deck's in `test_abilities`; real decks with rules too), `test_replay`, `test_gate` (1000 games of the real
  decks, vanilla (5.2) and with rules (5.3); writes the replay of a failure).
- `test_real.hpp` + `test_cards_vi.cpp` / `test_cards_zed.cpp` (5.3): real cards from the fixture with their rules
  (`real(name)`), `real_deck` (test frame, Zap, the deck's runes), `fund` (pool energy/power), `zap_kill`; one
  `TEST(RiftboundVi|Zed, <Card>)` per card, and 40 random games of everything a deck file registers
  (`registered_deck`). Text for the frame's legend and battlefields: `s.obj(id).card = real(…)` / `set_field`;
  `next_turn_of(s, p)` plays on to p's next Main phase. `put()` may take a copy from the trash: `in_hand()` instead. Combat: `at_field` (a unit
  holding a battlefield) + `ready_in_base` + `move_units`, then `both_pass`/`settle`; `close_showdown`.
- Every game test has the ctest label `riftbound`: `deploy/test.sh --riftbound [--pi]` (skipped without).
- `buddy-game --random [--rules] --games 1000 --seed 0 --deck decks/vi-unleashed.txt --deck decks/zed-vendetta.txt`
  on the ASan build (`build-test/services/game/buddy-game`); `deploy/run.sh game --random …` on the Pi (Release).
  #32 on the Pi (Release): with rules 2.97 ms per game (56k actions/s, 167 actions) vs vanilla 0.74 ms; `traits()`
  asking every object's passives is the likely cost. Gate with rules under ASan: 73 s on the VM.
  After #26 on the VM (Release): vanilla 0.63 ms per game (#25: 0.58), 17 turns (scoring ends games; before #25 they ran ~180
  turns to burn out); ability decks ~7 activations, ~9 triggers, ~36 choices per game. #27 on the Pi (Release):
  0.72 ms per game, `sizeof(GameState)` 896 B, a copy 0.83 µs (printed after every `--random` run).
- A failing random game: `replay-<seed>.txt` in the current directory → `buddy-game --replay <file>` (or by hand:
  the action lines are `<kind> [<obj>|-] [<n>]`, cut it down to find the action).

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
- Played flags are `played::flow` etc. (`play` is the engine's function name).
- Tokens append objects: `ObjId`s stay valid, but `s.objects.size()` grows during a game; a token off the board is
  `Removed` forever (its id is never reused).
- Vanilla mode reads stats and `Card::timing`, nothing else of the text (no keywords: they come from `CardRules`): `rules_of()` is nullptr for every card;
  without vanilla every card needs rules (D21), `test::ruled()` gives test decks empty ones.
- Ids follow the shuffled deck order (#27): tests that compare option lists sort the ids they `put` (options come in
  id order), and a deck's order is compared by card, not by id.
- Battlefields are not in any zone list (`Battlefield::card`); a base list is its controller's base, so "in another
  player's base" (323.6) can't be represented.
