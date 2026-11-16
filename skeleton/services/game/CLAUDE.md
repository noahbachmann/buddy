# services/game – Riftbound engine quick reference

What the engine does **now** (state after #73: 5.3, 5.4 and 5.5 done: arena, v1 greedy, engine speed, v2 ISMCTS, table
moves; 5.6 engine side: binding revealed cards, public view, replay reveals; the session with Buddy's side and Dev's
real cards, SimDev; the panel's payload, Dev's commands, voice lines; the session soak (the gate) and terminal play;
Dev declares what a cost discards, the referee never picks from Dev's placeholders)
and where the next work plugs in.
Why it is built this way: `docs/plans/05-riftbound.md` → "5.2 design", "5.3 as built" (with the rulings, confirmed by Dev),
"5.4 design", "5.5 design"/"as built". Sub-issues: 5.2 #20, 5.3 #33, 5.4 #39, 5.5 #45. Rules text: `~/buddy/data/riftbound/core-rules-2026-10-09.txt`
on the VM (grep the rule number); full card text: `~/buddy/data/riftbound/cards.json` (VM and Pi, not in Git).

## Files (`riftbound/`, pure library, no MQTT)

| File | What |
|---|---|
| `card_db`, `code`, `deck` | Card data, printed codes, decklists (5.1). `Card::timing` = leading `[Action]`/`[Reaction]` (`printed_timing()`); `Card::rules` = its `CardRules` (nullptr = not implemented) |
| `state` | `GameState` value: objects (`ObjId` + `gen`, 124; `attached` = the unit an Equipment hangs on), zone id lists, `active` (ascending ids in board and legend zones, `active_zone()`: what rules loops walk instead of every object; kept by `move_to`/`create`), pools, battlefields (`controller`, `contested`), chain (`ChainItem` with targets, cost, step counters, unit location `to`, `play` = `played::` bits: Flow, Ambush, token to base / exhausted; `rep` = Repeat execution), `passes`, `showdown` (`Showdown`: focus, passes, attacker, damage assignment), tasks, decision, RNG, and the ability data: `triggered`, `effects`, `delayed`, `uses`, `links`. `move_to` (raw zone change, no events; carries attached gear, detaches it when its unit leaves the board, a token off the board → `Removed`), `create` (new object), `draw`, `burn`, `channel`, `recycle`, `bind`/`set_hand_order` (5.6, see Hidden information), `turn_state`, `chain_priority`, `lethal_damage`, `check_invariants` |
| `rules` | `CardRules` (the hooks a card implements), `AbilitySpec` (`builtin`: Empower, Equip), `TargetSpec`, `CostMod`, `Mod`; `Resolution` (what `resolve()` works with); `active_rules` (rules that work where the card is, none while attached); targets (`candidates`, `still_valid`, `target_slots`/`target_spec`: once per Repeat), `optional_costs` (card's, Accelerate, Repeats), `total_cost` (Flow base, Deflect), `payable`, `completable` (355.16: targets + cost can still work out); `predict`, `resolve_builtin` (Vision, Empower, Equip); `filters::`; `rules_of`/`active_rules` inline (hot) |
| `actions` | Game actions abilities see: `kill`/`kill_all` (Die → Died), `deal`, `heal`, `exhaust`, `ready`, `recall`, `move` (Moved events, contests the destination), `contest`, `stun`, `buff`, `discard`, `empower`/`disempower`, `banish` (Banished event), `return_to_hand`, `add`, `pay_energy`, `attach`/`detach`, `play_token`; `emit` (queue triggers, Vision), `replaced` (replacement effects); layers `traits`/`might`/`has` (Assault/Shield while designated, Might Bonus of attached gear); `mighty`; `give`/`give_might`/`double_might`, `expire`, `prune`; `uses`, `link`/`linked`; `has_units_at` |
| `tokens` | The decks' tokens with their rules: `bird()` (Deflect), `shadow_clone()` (attack trigger), `gold()` (Add [A]) |
| `pay` | Rune payment of Energy, domain Power, then any-domain Power ([A]); which rune to recycle is the only choice (`Recycle` decision, one pick per domain) |
| `engine` | `new_game`, `legal_actions`, `apply`, `concede`, `playable`, `describe`; the loop `run()`, `cleanup()`, `run_task()`, `finalize()`, `resolve_step()`, `place_triggers()`; showdowns and combat (`begin`, `pass_focus`, `score`, `establish_control`, damage assignment `assign_options`), Standard Move (`can_move`) |
| `random_game` | `play_random()`: uniform random seats, `GameChecks` after every action (invariants, card count, turn start); vanilla or with rules; counts spells, chain plays, activations, triggers, choices, moves, showdowns, combats, scorings; `log` = the actions (failing one included) |
| `hidden` | `view(s, p)`: objects p doesn't know (`Object::known` bit, set by `move_to`/`recycle`/`predict`) get `hidden_card()`, RNG reset; not playable. `public_view(s)`: only cards known to everyone (the panel's source, 5.6). `determinize(view, seat0, seat1, rng)`: per owner the decklist minus visible cards, shuffled onto the hidden objects (rune deck: runes), RNG reseeded |
| `replay` | `Replay` (seed, vanilla, both decklists' text, actions; 5.6: `buddy` seat + `reveals`: `bind <obj> <code>` / `order <p> <ids…>` lines before the action they precede, `apply_reveal` with the game's `BindMemory`) ⇄ text (`parse_replay`, `to_text`: annotated by replaying), `run_replay` (GameChecks after every action; `buddy-game --replay`) |
| `rng` | xoshiro256\*\*, own `below`/`shuffle` (portable replays) |
| `ai/player` | `Player` (`begin(seat, decks)`, `choose(view, actions)`: never the referee's state; `iterations()` of the last search), `RandomPlayer`; `ActionKey` / `key_of` (hand and trash cards by card, the rest by id), `distinct` |
| `ai/arena` | `play_game` (forced moves applied unasked, `GameChecks` after every action, per-seat `SeatStats`: legal actions per decision, forced, time and iterations per asked decision, Choose runs), `play_match` (`PlayerFactory` per game, `pairing()`: seed S + i/2, players swapped every game, decks every other seed; `--jobs` threads, results independent of them), `wilson`, `percentile` |
| `ai/eval` | `eval(s, p, Weights)`: points (steeper past 5), control, units + Might (more at battlefields), gear, hand size, runes in base, legend ready; mine − theirs, win/loss ±∞ |
| `ai/greedy` | `greedy_choice(s)` on a full state: per `distinct` action apply + `play_on` to quiescence (anyone's Neutral Open priority or game over; own sub-decisions nested greedy up to `depth` 2, otherwise pass/decline/first) + `eval`; EndTurn = eval of the state now; ties → first. `mulligan_choice` (Energy ≥ 5 set aside). `GreedyPlayer` = one determinization per decision |
| `ai/ismcts` | `ismcts_search(view, actions, decks, seed, IsmctsOptions)`: SO-ISMCTS. Per iteration a fresh `determinize`, descend the tree (edges = `ActionKey`, forced moves skipped as in the arena, UCB1 with availability counts, rewards from the chooser's side), expand one untried action, playout (`playout` random actions within the turn, then pass/decline to quiescence), reward = sigmoid(`eval` / `scale`), win/loss 1/0. The root uses the given actions merged by key on the view (a determinization would key an opponent's hidden card differently). Budget: `iterations` (split over `threads`, reproducible) or `budget_ms`; root parallel, visits summed, most visits wins, ties → action order. `IsmctsPlayer`: mulligan by greedy's rule |
| `cards/` | The decks' cards (5.3, #33): `vi_unleashed.cpp`, `zed_vendetta.cpp` (one `CardRules` per card, its text above it, an `Entry` table at the end), `common.hpp` (`keywords({…})`, `cost()`, `discard()`, `count_named`, `battlefield_of`, `conquered_with`, `played_me`, `loot`, `Deathknell` base); `rules_for(name)`, attached by `CardDb::parse`: every loaded db (the test fixture too) has them |

## Table moves (5.5, `game_core`: `table_moves.*`)

What Buddy's physical cards do per referee step, as table commands (`shared/table_protocol.hpp`; the table side:
`services/table/CLAUDE.md`). `riftbound::move_log` (state.hpp): a thread-local log `move_to`/`create` append to,
installed by `MoveRecorder` around the referee's `new_game`/`apply` only (search and players never log; off it costs
one load per move; the `--random` digest is unchanged). `table_setup` takes the created objects off new_game's log
(legend, champion, decks bottom → top). `TableMoves::step(after, log)`: the seat's cards, hops through the chain or
set aside collapsed (hand → chain → base = one play, mulligan = play → deck bottom after the redraw), deck → place
= `deal` (consecutive ones merged), Vision = `peek` when the engine shows the seat its top card (a `peek` entry in the
log from `predict`: the step ends with the keep-or-recycle decision open), then `peek_done` (deck top or bottom) first
in the answer's step (`TableMoves` remembers the card on the scan spot), deck → deck bottom unpeeked = `peek` +
`peek_done`, created = `token`, a token gone =
`remove`, hand → anywhere = `play`, else `move`; then exhaust/ready where the state differs from what the table shows.
`nearest_copy(s, a)`: of same-card copies in hand play the lowest index (the table plays exactly the id asked, so the
engine's hand order stays the pile's): game (5.6) calls it on Buddy's chosen action before `apply`.
`workload_game` / `buddy-game --table-workload` = the soak's input. Tests: `tests/test_table_moves.cpp` (own
executable `buddy-game-table-tests`, links `table_core`).

## Session (5.6.2/5.6.3, `game_core`: `session.*`, `sim_dev.*`)

One game, message-driven, no MQTT/threads/clock. Inputs: `start`, `on_table(event)`, `on_dev(option)` (one of
`dev_options()`), `on_reveal(cards)` (the cards of `prompt()`), `on_ai(id, action)`, `resume`, `concede(p)`; after
each, `take()` → `Outputs` (table commands, at most one in flight; an `AiRequest` with `view(s, buddy)`; voice lines;
replay lines, header first). `Status`: setup → dev_to_act / dev_to_reveal / thinking / acting → paused → over. Dev's
input that can't be: `Refused` (the reason), nothing changes.
- **Step:** `next = s; apply` under `MoveRecorder` on a copy of `TableMoves`, deals split into single cards (a failed
  deal reports none of the cards it placed). The step's items = Buddy's table commands + Dev's prompts, merged by log
  position (`TableMove::at`; a split deal keeps its first card's). Each `deal`/`peek` code → `bind(s, obj, card,
  buddy, bound_.keep(…))`; a changed card marks the step stale, and before the next item (or the commit) it is
  re-applied: the items already done must be a prefix, else **desync** (pause for good, `# desync:` in the replay).
  Commit: `s = next`, `moves_` advances, the action line. Binds go to the replay as they happen (`before` = the next
  action); `bound_` (`BindMemory`: kept while the object sits where it was bound and Buddy doesn't know it) is bind's
  `keep`, run_replay keeps binds the same way.
- **Dev's real cards (#49):** Dev's hidden objects (hand, deck, rune deck) hold placeholders, never shown or trusted.
  `dev_options()` (free: the leak test reruns it): actions on cards Buddy knows as they are; Dev's hidden hand cards by card: per candidate (bind's pool
  for the representative `h` = Dev's first hidden hand card, Priority, or the first hidden one offered, Choose; runes
  excluded; a "discard a spell" Choose only spells) a copy with `h` bound to it; Priority: its `legal_actions`' play
  of `h` → an option with that card; Choose: `Pick h`. Mulligan by count (the first hidden pick). Candidates in
  decklist order (`dev_pool`), the list sorted by group, declared card, action (#50): the same list for every
  arrangement of the placeholders, so the panel's `i` is stable and says nothing. One option in all → applied unasked
  (`dev_forced`). Choosing an option with a card: bind + `bind` line, `named_` (no prompt for it) and `bound_`.
- **Dev declares, the referee records (#73):** the referee never picks from Dev's placeholders. Dev's games run with
  `GameOptions::ask_discard` (Dev's bit): every discard cost of Dev's is a Choose, Dev picks by card. An action whose
  cost discards a spell (`needs_spell`: Mel's Empower) is never offered plain: one option per spell Dev could pay
  with (`discards()`: "Mel, Defiant Soul: Empower (discard Ruthless Strike)"), the spells Buddy knows in Dev's hand
  (their own object) and the pool's spells on `h`; choosing one binds it and sets `declared_`, and the Discard
  choice that follows is answered with it (forced). So a "discard a spell" is never a prompt and never depends on
  whether the placeholders hold a spell.
- **Prompts:** objects of Dev's that Buddy didn't know before the step and knows after it, unless named; placed where
  each first lands in public; a row of the same origin (`Hand` = discarded unasked, `Deck` = burned, `Runes` =
  channelled) is one prompt. `on_reveal`: binds on a copy (kind checked: runes, battlefields, the rest), re-applies
  the step, the items up to the prompt must stay; then commits the binds. Dev's battlefield = the setup step's first
  item (before both mulligans; `bind` swaps it with a removed one).
- **Setup:** `setup` with `sim_deck`/`sim_runes` (home-printing codes); the opening deals from new_game's log run as a
  step without action, bound on `s` directly. new_game drew the hand already, so a dealt card the guess put on a
  hand object not dealt yet swaps from there (`bind` during Setup, #50; `BindMemory` keeps dealt hand cards).
- **Ids:** `id_`/`obj_` engine object ⇄ table id (deal/peek/token give them; into a deck, `remove`, `peek_done` forget);
  `table_hand_` = the belief's hand as the session saw it (deals, plays, returns, `hand_corrected`). At every commit
  `reconcile_hand()`: a different order → `set_hand_order` + an `order` line; not a permutation → desync.
- **Failures:** `not_in_hand` on the first command of Buddy's chosen play → step dropped, AI asked again (twice at
  most, then pause); `unreadable` on a step command → once more, then pause; anything else → pause (`Pause` says why);
  `resume` = `rescan_hand`, then the step's next command again (setup failed: the setup again). `hand_corrected`:
  `replaced` → rebind + "I misread a card"; `extra` → back on the deck top (Play → `deck_top`), unless its code is a
  `missing` card's (found again: same object); `missing` → pause after the command, a rescan that still misses it
  pauses again.
- **Voice and log (#50):** at every commit `narrate(before, a)`: a log line (`log()`, last 30: `describe` on the
  public view after the action, with the decision answered); Buddy's **action group** = a Priority play / activation
  / move plus Buddy's Choose/Recycle answers right after it, one line when someone else decides ("I play Zed to
  Zaun Warrens", "… on Vi" from the chain item's targets, "I empower Mel", "I move Vi and the Bird to …", "I attack
  at … with …" when the destination held enemy units; a play's battlefield from where the card ended up); passes and
  Dev's moves silent. Scoring: a `Player::scored` bit new this step ("I conquer X. 5 to 3.", scorer first; hold =
  the turn player's, in a new turn or before Main). `pause_text(p)` = the spoken pause line (the panel shows it, not
  `pause_message`: that may name a card).
- **`SimDev`** (tests, the soak, bot games): `truth_` per object (a shuffled permutation of Dev's hidden cards,
  runes and battlefields; `shuffle = false`: the placeholders). `choose`: a `Player` on `true_state(s)`, mapped to an
  option; hidden hand cards are interchangeable, so it swaps true cards when the option names another object
  (`declared` for a declared discard: plays the true game to its Discard pick, takes the option with that card and
  swaps the truth onto its object). `reveal`: the true cards.
- Tests `tests/test_session.cpp` (`buddy-game-session-tests`, label `riftbound`, ~60 s wall under ASan): Dev = a
  SimDev; fault-free games (both decks from both seats), each sim fault alone, every failure path forced by a `Flaky`
  backend wrapper or by moving the sim's cards by hand, a forced desync (`Session::on_moves` hook), Dev's
  battlefield and refusals; `SimDevGames0`–`4` (50 games with a true deck). After every step: engine = belief (hand
  order, ids, codes, spots, exhausted, counts), belief = truth with the hand as a multiset (a disturbed pile stays
  unnoticed until a play mismatches), Dev's known cards = SimDev's truth, none learned unasked; at Dev's priority
  (SimDev games) the offered cards = exactly the possible ones. At the end a rescan, then strict; the session's
  replay replays to the same state; SimDev games: the game with Dev's true cards from the start replays legally
  into the same state (`true_replay_problem`, with Dev's discards asked in both since #73). The harness is `game::SimGame` (game_sim, #51); `tests/session_game.hpp` wraps it as `Game`
  with the `Flaky` backend; #50 adds `DevOptionsDontDependOnHiddenCards` and `VoiceLinesPerActionGroupAndScoring`,
  #73 `DevDeclaresTheSpellACostDiscards`.

## Public state and commands (#50, `game_core`: `public_state.*`, `game_host.*`)

- **`public_state(session)`** = `buddy/game/state` without `v` and catalogue, built from `public_view(s)` only:
  `status`, `pause {reason, message}`, `winner`, `turn`, `phase`, `turn_player`, `victory`, `decision {player, kind,
  purpose}`, `players.{buddy,dev}` (`points`, `legend`, `champion`, `base` (objects: `id`, `card` code, `exhausted`,
  units `might` (layers), `damage`, `buffs`, `stunned`; `attached_to`, `empowered`, `keywords` ("Assault 2")),
  `runes` per domain ready/exhausted, `pool`, counts of `hand`/`deck`/`rune_deck`, `trash`/`banishment` codes),
  `battlefields` (`card`, `owner`, `controller`, `contested`, `units.{buddy,dev}`, `facedown` count), `chain`
  (`kind`, `card`, `controller`, `finalized`, `targets`), `showdown`, `options` (DevToAct: `i`, `text` =
  `option_text` on the public view, `group`, `card` only when named), `prompt` (`kind`, `n`, `candidates`: code +
  `left`, runes with `domain`), `log`. Players are `buddy`/`dev`, never seats. `catalogue(decks)`: every card + the
  three tokens by `code_of` key. The overload with `(s, options)` is for the leak test.
- **`GameHost`**: one `Session` (+ `SimDev` for `dev: greedy|random`, answered inside at once). `command(json)` →
  `Reply {ok, message}`: `new {buddy: vi|zed, seed?, dev}` (refused while a game runs; Buddy = seat 0, the first
  player is the engine's coin flip), `act {v, i}`, `reveal {v, cards: [code…]}`, `resume`, `concede`; an unknown
  field or value, a stale `v`, the wrong status → refused, nothing changes. `on_table`, `on_ai` pass through.
  `take()`: table commands, AI request, voice, replay lines (`new_game` = a new file), and the payload when its game
  part changed (`v` + 1, catalogue in every payload: web's patches carry only changes).
- Tests: `test_public_state.cpp` (leak test: after every input of 4 SimDev games, the payload is the same with
  Dev's placeholders and Buddy's hidden cards rearranged; every `card` is in the catalogue), `test_game_host.cpp`
  (bot games, a human Dev playing only by `i` and the prompt's candidates, every refusal). `BUDDY_GAME_STATE_DUMP=<file>`
  makes `HumanDevPlaysByCommands` write a turn-6 payload.

## Soak and terminal play (#51, `game_sim`: `sim_game.*`, `sim_terminal.*`)

`game_sim` = `game_core` + `table_core` (the service links only `game_core`: its table is on MQTT).
- **`SimGame`**: one session game the way the service runs it: Session, in-process `Table` + `Sim` (optional `wrap`
  of the backend: the tests' `Flaky`), Buddy's `Player` (greedy by default), Dev a `SimDev`. `run()` until over, a
  desync, 20 resumes or `stop_when`. After every committed step: engine = belief (hand order, ids, codes, spots,
  exhausted, counts), belief = truth (the hand as a multiset; skipped while a peeked card is held: a double deal under
  it shows only at `peek_done`), Dev's known cards = SimDev's truth and never learned unasked (a prompt, Dev's
  choice, or a `bind` line: a choice with one card left in Dev's pool binds it unasked, a deduction). Optional:
  `check_candidates` (the offered cards = the possible ones), `leak_every` n (`leak_problem`: payload + Dev's options
  on rearranged hidden cards, #50). At the end: `final_check` (rescan, then strict), `replay_problem`,
  `true_replay_problem`. `problems` = unrecovered, `leaks`, `pauses`. `rearranged`, `dev_fixed` live here too.
- **`soak()` / `soak_game()`**: game i = seed S + i, Buddy in seat i % 2, decks swapped every other pair; true deck,
  leak check every 7th input, candidates on; `--jobs` threads, results independent of them; never throws (an
  exception is a problem). `SoakGame`: problems, leaks, desyncs, `SessionStats`, table recoveries/scans, faults,
  prompt cards, `taps()` (asked decisions + per prompt its cards + 1 + resumes), ms, the replay of a failed game.
  `print_soak_report` = the table in the plan ("5.6 as built" → #51).
- **`buddy-game --sim --games N [--seed S] [--jobs J] [--faults …] [--dev greedy|random] [--buddy greedy|ismcts]
  [--iterations N]`**: the soak; a failed game writes `sim-replay-<seed>.txt` (`--replay` reads it). Gate: 1000
  games under stress, 9.6 s on the VM (Release, 8 jobs).
- **`buddy-game --sim [--faults …] [--buddy …] [--seed S]`**: `TerminalGame` = a `GameHost` (Buddy = deck A, Dev
  human) + the sim table + Buddy's AI, synchronous; `render(payload)` prints the board from the payload alone,
  `prompt_answer` turns candidate numbers or codes into a `reveal`. Lines: a number, numbers/codes at a prompt,
  `resume`, `concede`, `board`, `help`, `quit`; a refused line changes nothing.
- Tests: `test_sim_soak.cpp` (`RiftboundSimSoak.Games0`–`9`: 100 stress games, 35–45 s each under ASan),
  `test_sim_terminal.cpp` (typed games to the end, bad lines, prompt answers).

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

Priority actions: `Play` (hand cards, and trash cards with Flow, `playable()`: type, timing or Ambush (a Reaction to a battlefield where the player has units), `can_play`, `completable`), `Activate` (obj = source on the board or legend zone, not attached, n = ability: controller, timing, `usable`, `per_turn`, Empower only if not Empowered, `completable`), `Move` (Neutral Open main phase only: one per destination with a unit that can go, n = battlefield or `to_base`; the units are a `Move` choice), then `EndTurn` (Neutral Open) or `Pass` (closed: chain priority; Showdown Open: pass focus). `Choose` decisions: `options` in a fixed order, picks ascending, auto-finished at `max`, `Done` from `min`; the answer goes to `GameState::answer` for whoever asked. `apply` accepts only what `legal_actions` offers and throws `IllegalAction` with the state untouched; it checks the one action (`is_legal`), not the whole list: a new action kind or condition goes into both (`RiftboundRandom.ApplyAcceptsExactlyTheLegalActions` probes them against each other).

## Hidden information (128)

`Object::known`: who knows which card it is. Public zone → everyone; into a hand → + owner; facedown → + controller;
into a deck or set aside → unchanged (a card seen going to the bottom stays known); `recycle` of 2+ → nobody (random
order); `predict` → + predictor. Setup creates deck objects after shuffling, so ids say nothing about cards. A card
rule that looks at or reveals a hidden card (5.3) must set `known` too; `check_invariants` checks public and hand
cards. `test_hidden.hpp`'s `views_along_a_game` checks after every action that a determinization is valid, has the
same view and gives the deciding player the same legal actions: run it on any new deck.

Revealed identities (5.6): the referee doesn't know Dev's real cards or the order of Buddy's physically shuffled deck,
so the engine's cards for objects Buddy doesn't know are one possibility. `bind(s, obj, card, knower)` (knower = Buddy's
seat) swaps `Object::card` with the first object (by id) of the same owner that holds `card` and sits in the pool:
the owner's main and rune deck unknown to the knower, plus the hand if the owner isn't the knower. `obj` must not be
public; it may be the knower's own hand card (a misread at draw: same call). Runes only with runes. Zones, `gen`,
`known` unchanged, so views, determinizations and per-owner card counts stay valid; binding the knower's own cards
changes neither player's view (`binds_along_a_game`). `set_hand_order` = the table's order after a disturbed pile.
`keep`: objects bound earlier that must keep their card. `BindMemory` (#49) tracks them: an object stays kept while
it sits where it was bound (same `gen`) and the knower doesn't know it, or it's in the knower's own hand. During Setup
a card of the knower's own hand also swaps with another of its hand objects the opponent doesn't know and `keep`
doesn't hold: the opening hand is the engine's guess until the table deals it (#50). Two copies dealt in one step are bound on the
state before it, where the first still sits in the pool; Dev's discard picked by card stays in hand until paid. A
battlefield binds with one of its owner's removed battlefields (setup, Dev's choice; it's public otherwise).

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
  card; the keywords join the unit's own in `traits()`). "Move a unit" without a destination: a second slot with
  `TargetSpec::location` (+ `pick`), picked as it's played; `r.location(slot)` = battlefield index or `to_base`.
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
and recycles the runes in pay.hpp's order (Dev's ruling): no Recycle decision outside the chain.

## Invariants (`check_invariants`, run after every random-game action)

Every object in exactly one zone list matching its zone/owner/controller/where; `active` = the objects in active zones, ascending; public cards known to everyone, hand
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
  ability-deck games in `test_abilities`; `apply` accepts exactly `legal_actions`), `test_ai` (keys, players only get views, matches reproducible across
  jobs, eval, greedy on hand-built states: conquer, lethal, no trade down, no own target; greedy vs random 8 games;
  ISMCTS reproducible with 1 and 3 threads, takes the win in 1, ≥ 1000 searched decisions in ISMCTS-vs-ISMCTS games all legal: ~30 s under ASan), `test_hidden` (knowledge, view, determinize; views along random games, the
  ability deck's in `test_abilities`; real decks with rules too), `test_replay` (incl. binds/orders round trip), `test_session` (#48, own executable with `test_public_state`,
  `test_game_host`, `test_sim_soak`, `test_sim_terminal`), `test_gate` (1000 games of the real
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
  Prints a digest over every game (winner, turns, actions): an engine change that must not change play keeps it
  (seeds 0–999: rules `42705b80131137f7` since Gust Monk's ruling, `3fa44f83335b8657` at #36; vanilla `d04c2a02e1c41238`), and "engine only" = the same games
  again with `legal_actions` + `apply` alone (what a playout pays). #36 on the Pi (Release): with rules 1.17 ms per
  game, 143k actions/s with checks, 244k engine only (#32: 2.97 ms, 56k / 64k); vanilla 196k / 448k. Gate with rules
  under ASan: 54 s on the VM. Profiling: `perf` on the VM (Release, `-fno-omit-frame-pointer -g`; the Pi has none).
  After #26 on the VM (Release): vanilla 0.63 ms per game (#25: 0.58), 17 turns (scoring ends games; before #25 they ran ~180
  turns to burn out); ability decks ~7 activations, ~9 triggers, ~36 choices per game. #27 on the Pi (Release):
  0.72 ms per game, `sizeof(GameState)` 896 B, a copy 0.83 µs (printed after every `--random` run).
- `buddy-game --match <a> <b> [--games N] [--seed S] [--jobs J] [--iterations N | --budget-ms M] [--threads T]
  [--tune c=…,playout=…,scale=…,<weight>=…]` (players `random`, `greedy`, `ismcts`; default 200 games,
  jobs = cores, decks = the first two; the ISMCTS flags apply to both seats if both are `ismcts`): wins with Wilson CI per player and deck, decision times, branching. #35 (seed 0):
  random vs random 1000 games 479:521, decks 517:483; greedy vs random 190/200 (seed 1000: 196), greedy vs greedy
  Zed 146:54. Pi: greedy decision p50 0.24 ms, p95 3.4 ms, max 30 ms; identical games on VM and Pi.
  #37/#38: ismcts (1000 iterations, c 0.4, playout 10) vs greedy 160/200 (seeds 1000–1199: 325/400); ~55 µs per
  iteration per core. Service default (5.6): `--budget-ms 2000 --threads 3` (the 4th core stays for ears), ~78k
  iterations per decision on the Pi. Tuning results and what didn't help: plan → "5.4 as built".
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
