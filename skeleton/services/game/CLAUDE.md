# services/game – Riftbound engine quick reference

What the engine does **now** (state after #24) and where the next work plugs in. Why it is built this way:
`docs/plans/05-riftbound.md` → "5.2 design". Sub-issues of 5.2: #20. Rules text: `~/buddy/data/riftbound/core-rules-2026-10-09.txt`
on the VM (grep the rule number); full card text: `~/buddy/data/riftbound/cards.json` (VM and Pi, not in Git).

## Files (`riftbound/`, pure library, no MQTT)

| File | What |
|---|---|
| `card_db`, `code`, `deck` | Card data, printed codes, decklists (5.1). `Card::timing` = leading `[Action]`/`[Reaction]` (`printed_timing()`); `Card::rules` = its `CardRules` (nullptr = not implemented) |
| `state` | `GameState` value: objects (`ObjId` + `gen`, 124), zone id lists, pools, chain (`ChainItem` with targets, cost, step counters), `passes`, `showdown`, tasks, decision, RNG, and the ability data: `triggered`, `effects`, `delayed`, `uses`, `links`. `move_to` (raw zone change, no events), `draw`, `channel`, `recycle`, `turn_state`, `chain_priority`, `lethal_damage`, `check_invariants` |
| `rules` | `CardRules` (the hooks a card implements), `AbilitySpec`, `TargetSpec`, `CostMod`, `Mod`; `Resolution` (what `resolve()` works with); targets (`candidates`, `still_valid`), `total_cost`, `payable`; `filters::` |
| `actions` | Game actions abilities see: `kill`/`kill_all` (Die → Died), `deal`, `heal`, `exhaust`, `ready`, `recall`, `stun`, `buff`, `discard`, `empower`; `emit` (queue triggers), `replaced` (replacement effects); layers `traits`/`might`/`has`; `give`/`give_might`/`double_might`, `expire`, `prune`; `uses`, `link`/`linked` |
| `pay` | Rune payment of Energy/Power; the domain is the only choice (`Recycle` decision) |
| `engine` | `new_game`, `legal_actions`, `apply`, `concede`, `playable`, `describe`; the loop `run()`, `cleanup()`, `run_task()`, `finalize()`, `resolve_step()`, `place_triggers()` |
| `random_game` | `play_random()`: uniform random seats, all checks after every action; vanilla or with rules; counts spells, chain plays, activations, triggers, choices |
| `rng` | xoshiro256\*\*, own `below`/`shuffle` (portable replays) |

## The loop (`engine.cpp` → `run()`), in this order every iteration

1. `cleanup_due` and nothing resolving (321) → `cleanup()` (323 in rule order; lethal damage via `kill_all`, `prune`; re-arms the flag if it changed something, 322).
2. A decision is open → stop.
3. An item `resolving` → `resolve_step()`: `CardRules::resolve` until `Done` (then off the chain, a spell to the trash, `Activated` event) or `Wait` (a decision is open).
4. A pending chain item → `finalize()` the oldest, a step machine in `ChainItem::step`: May (triggers) → Targets (one Choose per slot, forced picks automatic) → Optional costs → total cost (356) → Discard / RecycleTrash costs → runes (`Recycle`) + self costs → `finished()`: Targeted events, permanents enter (`enters_ready`) with a Played event, `add` abilities resolve at once. A trigger without targets or payment leaves (403.4, 405.2).
5. `triggered` not empty → `place_triggers()`: turn player's first, each player orders their own (TriggerOrder).
6. Chain not empty: `passes == 2` → start resolving the newest; else Priority for `chain_priority()`.
7. `tasks` (stack, back runs next) → `run_task()`: Mulligan, Awaken (resets `uses`, `deaths_this_turn`), Beginning, Channel, Draw, Main, Ending (PhaseStart events), Expiration (Ending Special Cleanup incl. "this turn" expiry; again after the chain while anything went on it, 317.2.f; then the next turn). The chain comes before tasks: a Beginning-phase trigger resolves before Channel.
8. No chain, Main phase, no showdown → Priority for the turn player (335). Otherwise stop.

Priority actions: `Play` (hand cards `playable()`: type, timing, `can_play`, enough targets, payable), `Activate` (obj = source on the board or legend zone, n = ability: controller, timing, `usable`, `per_turn`, targets, cost), then `EndTurn` (open) or `Pass` (closed). `Choose` decisions: `options` in a fixed order, picks ascending, auto-finished at `max`, `Done` from `min`; the answer goes to `GameState::answer` for whoever asked. `apply` accepts only what `legal_actions` offers and throws `IllegalAction` with the state untouched.

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

## Where #25 plugs in

- Movement, showdowns, combat, scoring: emit `Moved`, `Conquer`, `Hold`, `Attack`, `Defend` events (add them to
  `EventKind`), end "this combat" effects with `expire(s, Duration::Combat)`, unit locations at play (355.2:
  `finished()` puts units in base).
- Triggered chains don't pass focus (346.1); `place_triggers` is where showdown focus needs care.
- Combat's Assault/Shield might comes from `traits().keywords` (layer 3 values already add up).

Known gaps (no deck card needs them; add when one does): replacement order (372) and "may" replacements (371.2) are
not decisions (fixed order: delayed newest first, then static by id); group targets (355.11), split damage (355.14),
component discounts (356.4.c), Add abilities during payment (357.1.a: Gold tokens can be used before playing instead,
but `playable` doesn't count them), abilities on the chain as targets, cost kinds beyond discard / recycle from trash
/ exhaust / kill / disempower self (Burn, "discard a spell": #26), dependency loops across objects (478.1.c: depth
limit). Deflect's targeting cost is #26.

## Invariants (`check_invariants`, run after every random-game action)

Every object in exactly one zone list matching its zone/owner/controller/where; single-card zones; pools ≥ 0; no
finalized permanent on the chain; pending items never below finalized ones; at most one item resolving, never a pending
one; Choose picks in bounds and ascending; Priority only for the turn player (open) or `chain_priority()` (closed),
never with a pending or resolving item, queued triggers, or after both passed; no decision before a due cleanup
(except inside a resolution); something to decide while the game runs. `play_random` also checks a constant object
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
  timing, cleanups), `test_abilities` (360–406, targeting, costs, layers, 750), `test_random` (50 games each: spell
  decks and the real decks vanilla; 20 ability-deck games in `test_abilities`).
- `buddy-game --random --games 1000 --seed 0 --deck decks/vi-unleashed.txt --deck decks/zed-vendetta.txt` on the
  ASan build (`build-test/services/game/buddy-game`); `deploy/run.sh game --random …` on the Pi (Release).
  After #24 on the VM (Release): vanilla 3.0 ms per game (2.4 before #24: the layers and trigger checks run in
  vanilla too), ability decks 25 ms per game (~150 activations, ~300 triggers, ~480 choices per game).

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
- `GameState::showdown` is only read (timing, 323.5); nothing starts showdowns or gives focus until #25. Setting it in
  a running game leaves nobody with priority.
- 323.5 (lose battlefield control) runs only in an open state: a cleanup right after `Play` is closed.
- Vanilla mode reads stats and `Card::timing`, nothing else of the text: `rules_of()` is nullptr for every card;
  without vanilla every card needs rules (D21), `test::ruled()` gives test decks empty ones.
- Battlefields are not in any zone list (`Battlefield::card`); a base list is its controller's base, so "in another
  player's base" (323.6) can't be represented.
