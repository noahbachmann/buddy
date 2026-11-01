# Phase 5 – Riftbound: plan

> A plan, not a guide: decisions, architecture and the order of work for Buddy playing Riftbound against Dev. Step
> guides (`docs/guides/05-XX-*.md`) replace the parts of this file as steps get built. ⚠️ = untested assumption.

## Goal

Dev plays Riftbound (Riot's League of Legends TCG) against Buddy with real cards. Buddy knows the rules, picks its plays
and handles its own cards: draw, scan, hand pile, play to the right spot.

The hardware needs the arm (4.1), the camera (4.2) and a card-handling mechanism, so it comes last. The software gets
built now and tested against a **simulated table**, the same way 2.3 motion was built before the Pico was wired.

**Done when (software, 5.1–5.6):** Dev plays a full game against Buddy on the web panel. Buddy's side runs on the
simulated table with injected scan errors, which Buddy detects and corrects. Buddy's hand never shows on the panel.

## Decisions (2026-10-31)

| Topic | Decision |
|---|---|
| Rules and play | **Own C++ rules engine, no LLM.** Code is the referee and search picks the plays. An LLM loses track of state over a long game, invents rules, can't be unit-tested and isn't offline. The only LLM work is at dev time: Claude writes the card implementations from card text, each with a test. |
| Prior art | alpharune (github.com/chorlick/alpharune, C++20, OpenSpiel) is a full Riftbound engine but has **no license**. It's reading material only, and none of its code or tests get copied. |
| Card scope | **Two fixed decks:** the Vi Champion Deck (Unleashed, Fury/Order) and the Zed deck from the Vendetta Showdown Decks (Zed vs Shen). Each is 56 cards: legend, 40-card main deck, 12 runes and 3 battlefields. That's ~50–70 unique cards in total. A card that isn't implemented is a hard error at deck load, never a silent stub. More decks get added one at a time later. |
| Default seats | Buddy plays Zed and Dev plays Vi; `new` takes both decks, so seats can swap. |
| Rules version | Core Rules dated **2026-10-09** (with the Radiance patch, effective 2026-10-16), from the official Rules Hub (playriftbound.com/en-us/rules-hub), which only serves the current document. Radiance changes nothing either deck uses. The PDF stays out of Git (file name keeps the date), and tests cite its rule numbers. A new document: diff it, then update. |
| Card data | Riftcodex REST API (`api.riftcodex.com`, no key; 1451 printings in 8 sets: OGN, OGS, SFD, UNL, VEN, promos). Fetched on the Pi like models, not committed. Signature and alt-art printings (`*`) map to the same game card. |

## The physical workflow (Dev's design)

- **Draw:** take the top card of the deck, scan its code (set + number, printed at the bottom of the card face, e.g.
  `OGN-142/298`), put it on **top** of the hand pile.
- **Hand pile:** only the **bottom** card can be taken. Buddy remembers the order, so it never has to look through
  the pile.
- **Play X:** feed cards from the bottom of the hand (H) onto a second pile (B) until X is at the bottom of H. Scan to
  confirm, play X to its spot, then merge the piles.
- **Mismatch:** if the confirm scan isn't X, scan the whole hand, fix the model and carry on.

### Pile model

Piles are ordered lists, bottom → top. Every physical action is one of five primitive ops, the same for the sim and the
hardware:

| Op | Effect |
|---|---|
| `put_top(card, P)` | card onto the top of P |
| `scan_bottom(P) → code` | read the bottom card of P without moving it |
| `feed(P → Q)` | bottom card of P onto the top of Q |
| `take_bottom(P) → zone` | bottom card of P onto the board |
| `stack(P onto Q)` | whole pile P onto the top of Q |

Playing X at index k: k × `feed(H → B)`, `scan_bottom(H)`, `take_bottom(H)`, `stack(H onto B)`. B gets the fed cards
in their original bottom → top order, so stacking H onto B leaves **the original order minus X**. Cost = k feeds. The
tracker also supports `stack(B onto H)` in case the mechanism only allows that direction (the result is a rotation:
still fully predictable).

**Recovery:** the confirm scan shows the wrong card → feed and scan every card H → B, rebuild the order, check the count
(catches two cards fed at once), stack back. If the scanned set of cards differs from the hand the rules engine
believes in (a misread at draw), table reports the corrected hand; game fixes its state and says so out loud.

## Architecture

```
 Dev (web panel, later voice) ──► [game] rules engine + AI ──► buddy/voice/say ("I play …")
                                     │ ▲   "draw", "play hand card X to battlefield 2"
                                     ▼ │   drawn / done / failed / hand corrected
                                   [table] piles + hand tracker ──► sim backend (now)
                                                               └─► scanner + arm via [motion] (Phase 4+)
```

- **`game`** (C++): rules state, legal moves, AI, and *which* cards are in Buddy's hand. Libraries: `riftbound` (engine,
  card defs, AI; pure, no MQTT) and `game_core` (session logic), executable `buddy-game`. Offline mode
  `buddy-game --sim` plays a whole game in the terminal, or bot vs bot with `--games N` for benchmarks.
- **`table`** (C++): physical zones and *the order* of the hand pile. It turns "play X" into pile ops, confirms with
  scans and recovers. The backend is swappable: `sim` now (virtual piles with seeded fault injection: misread, double
  feed, pile disturbed, wrong card drawn), hardware later. game never knows whether the table is simulated, so the
  MQTT boundary gets tested from day one.
- One service, one job: game decides, table handles cards, and later table drives `motion`, which only knows joints.

### Engine (`services/game/riftbound/`)

- `CardDb`: loads the pruned card JSON and maps a printing code to a game card. It parses `OGN-142/298` and
  `UNL-229*`.
- `GameState`: zones per player (main deck, rune deck, hand, base, battlefields, trash, champion zone, legend), runes,
  energy/power, points, turn/phase, chain, showdown. It has to be cheap to copy, because search clones it thousands of
  times per decision.
- **Partial information:** Dev's hand and deck order are unknown to Buddy. The engine is a referee that only learns
  Dev's cards when they're played or revealed. The AI samples the unknown cards from Dev's decklist (determinization).
- `legal_actions(state)` and `apply(state, action)`. Mid-resolution choices ("choose a unit") are actions too,
  so the AI, the panel and the tests share one loop ("5.2 design" below).
- Cards: one C++ class per card (`cards/<set>/<num>_<slug>.cpp`) on a small library of effect primitives (deal damage,
  draw, move, buff, ready/exhaust, grant keyword…).
- Deterministic RNG seeded per game. Seed + action log replays a game exactly, and that's the bug-report format.

### AI

- **v1 heuristic:** score every legal action with the board, points, card advantage and a lethal check, then take the
  best one.
- **v2 ISMCTS:** Monte Carlo tree search over legal moves, re-sampling Dev's hidden cards per iteration, with a time
  budget per decision (start: 2 s on 4 threads on the Pi 5 ⚠️).
- Tie-breaker for later: among equal plays, prefer cards near the bottom of the pile (fewer feeds).

### MQTT (`shared/topics.json`)

| Topic | Direction | Notes |
|---|---|---|
| `buddy/game/cmd` | → game | `new` (decks, seed), `action` (Dev's move), `concede` |
| `buddy/game/state` | game → | retained **public** view: board, scores, Dev's legal actions. Never Buddy's hand. |
| `buddy/table/cmd` | game → table | `draw`, `play {card, to}`, `move`, `rescan_hand`. `"web": false`: it moves the arm later (D19). |
| `buddy/table/event` | table → game | `drawn {code}`, `done`, `failed`, `hand_corrected {cards}` |
| `buddy/table/state` | table → | retained pile model (debug; the panel doesn't render it) |

**Dev's moves** come from a Riftbound page on the web panel: the board, Dev's legal actions as buttons, and pass/respond
prompts during showdowns and the chain. The same page works for real games (Dev taps what they played). Voice for common
moves ("pass", "I attack the left battlefield") comes after that.

## Steps

| Step | Content | Done when |
|---|---|---|
| 5.1 Card data & decks | `deploy/pi/fetch-cards.sh` → `~/buddy/data/riftbound/cards.json` (pruned, not in Git). Code parser, printing → card map, decklist format (Piltover Archive text). Both decklists in `services/game/decks/`; a small card fixture for tests in the repo. | `buddy-game --deck <file> --check` lists every card; an unknown code is a clear error |
| 5.2 Rules engine core | Turn structure, runes/energy/power, units, moves, battlefields, showdowns, combat, chain/priority, conquer/hold scoring and the win condition, mulligan. Keywords only as the two decks need them. gtest per rules section. | 1000 seeded random-vs-random games without assertion failures under ASan/UBSan |
| 5.3 Cards of both decks | Every card in both decklists, one test each. Dev reviews odd rulings. | both decks load, per-card tests green |
| 5.4 AI player | v1 heuristic, then v2 ISMCTS | v1 beats random in ≥ 90 % and v2 beats v1 in > 50 % of 200 games each; decision time on the Pi within budget |
| 5.5 `table` service + sim | piles, primitive ops, hand tracker, verify/recover, fault injection. Property tests: random op sequences with faults always end in the correct state. | 10k games' worth of pile ops with faults: zero unrecovered states; scans per play reported |
| 5.6 `game` service + panel page | MQTT wiring, Riftbound page, voice lines via `buddy/voice/say`, `buddy-game --sim` | the phase's "Done when" above |
| 5.7+ Hardware | card scanner, hand tray mechanism, card placement, reading Dev's cards | separate plan after Phase 4 |

### 5.1 as built (2026-10-31, #19)

- **Data:** `setup.sh cards` (= `deploy/pi/fetch-cards.sh`, curl + stdlib python3; the Pi has no jq) → 1304 printings,
  930 cards, 8 sets; one entry per line, re-run for new sets. `BUDDY_GAME_CARDS` moves the file.
- **Riftcodex quirks** the loader handles: 147 printing ids listed twice (VEN as an old and a new import, the old one
  sometimes misnamed: "Master of Shadows"; OPP regular and Metal) → newest entry wins. Legends are named
  `Vi - Piltover Enforcer`, variants carry ` (Alternate Art)` / ` (Signature)` / ` (Metal)` / ` (Overnumbered)`.
  Printings of one card disagree on region tags (`Ionia` missing on some) and reminder text → tags merged, text from
  the home printing (plain, main set over promo, oldest set, lowest number). Type, domains and stats must agree; a card
  where they don't is refused by decks (none today).
- **Codes:** printed `UNL • 229*/219`, `OGN • 007a/298`, runes `VEN • R01 • EN`; Riftcodex `unl-229*-219`. Every
  spelling parses to one canonical code (`UNL-229*`). OPP promos reuse numbers per base set (`OPP-009/219` Upstage
  Comedy, `OPP-009/221` Serrated Dirk): there the `/total` decides, without it the lookup says "ambiguous". A total
  that doesn't fit the number is reported as a probable misread: the scanner gets that check for free.
- **Decks:** Piltover Archive text (`Legend:`, `Champion:`, `MainDeck:`, `Battlefields:`, `Runes:`; `Sideboard:`
  ignored), a name or a code per line; checked against Core Rules 103 (counts, ≤ 3 copies incl. the champion, domain
  identity, champion tag, signatures, 12 runes, 3 battlefields), every problem at once with line numbers and "did you
  mean". Sources: Vi = Riot's [Vi & Vex Champion Decks](https://playriftbound.com/en-us/news/announcements/vi--vex-champion-decks/)
  article (it misspells "Carion Dredger"), chosen champion Vi, Hotheaded per Magical Meta; Zed =
  [Piltover Archive](https://piltoverarchive.com/decks/view/f1d209f6-d427-491e-a958-90291b357198) and Magical Meta, identical.
  Both checked against the boxes by Dev (2026-11-01). The files list names, not codes: a name is the card the rules
  need, a code only one of its printings (`--check` shows them all).
- **Commands:** `deploy/run.sh game --check` (all decks), `--deck zed-vendetta --check`, `--code "OGN • 007a/298"`.
  `deploy.sh game` installs the binary and decks without a unit until 5.6.
- **Tests:** `services/game/tests/data/cards.json` = every printing of the two decks' cards (text removed); edge
  cases (conflicts, bad entries) inline in `test_card_db.cpp`.

### 5.2 design (#21)

The engine in `services/game/riftbound/`, Core Rules 2026-10-09. Three goals: one loop for the AI, the panel and the
tests; a state that's cheap to copy (search clones it thousands of times per decision); every rule testable by its
number.

**State as a value** (`state.hpp`)

- `GameState` = both players' zones, all objects, battlefields, chain, task stack, open decision, turn/phase, winner
  and the RNG. Copying it copies the game; `operator==` is defaulted, so determinism tests compare whole states.
- **Objects by index.** `ObjId` indexes `objects` and stays with the physical card for the whole game (table tracks
  cards by it). `gen` counts the rules objects the card becomes (124: a new object on every move to or from a
  non-board zone). Targets, effects and delayed abilities hold an `ObjRef {id, gen}`, so "it" stops matching after a
  zone change for free (`alive()`).
- **Zones are id lists**, decks with back = top. Non-board zones are the owner's (056), a base is the controller's,
  a battlefield card sits in `Battlefield::card`, not in a list. `move_to()` is the only way a card changes zones and
  does 124's reset (damage, buffs, statuses, controller). `check_invariants()`: every card in exactly one list that
  matches its zone, occupancy of the one-card zones.
- **The only pointers** go to immutable data: `const Card*` into the `CardDb`, later `const CardRules*` into the card
  registry. No `std::function`, no `shared_ptr`. Zones are `std::vector` for now: ~110 objects, ~20 small
  allocations per copy. 5.4 measures before anything smarter (fixed-capacity vectors).
- **Temporary modifications** ("+2 might this turn", granted keywords) aren't object fields. They're continuous
  effects in a list on the state, applied through the layers (479, #24), as plain data: source `ObjRef`, rules +
  ability index, subject `ObjRef`, duration.

**Decisions** (`engine.hpp`)

- `new_game()` runs the engine until someone has to choose and stops at `GameState::decision` (kind, player, picks so
  far). `legal_actions(s)` lists the options in a fixed order; `apply(s, a)` answers and runs to the next decision.
  `apply` accepts only what `legal_actions` offers (one source of legality) and throws `IllegalAction` with the state
  untouched.
- Every choice is a decision: priority (play, activate, move, pass), targets, modes, payment (which runes), damage
  assignment, trigger order, mulligan, "may", and the turn player's pick of a battlefield in a cleanup (323.11).
  One player decides at a time (303.2).
- `Action` = kind + object (+ a small number later). Choosing several things is Pick … Pick, Done, **in zone order**,
  so each set has exactly one path: search never explores permutations (mulligan: at most 11 sets).
- Hidden information: the state is the referee's full truth; `view()` and `determinize()` (#27) give a player's
  knowledge and states consistent with it.

**Payment** (`pay.hpp`, 357, 445): the player picks *what* to play; the engine pays, so costs don't multiply the
action list. `Play` is offered only when the cost is payable (pool + runes). Then, fixed:

1. The pool first (it empties anyway, 167): Energy; Power of a matching domain, then universal.
2. Power still owed: recycle runes of a matching domain (164.2.b), exhausted before ready ones (a ready rune can still
   give [1] this turn).
3. Energy still owed: exhaust ready runes (164.2.a), the ones about to be recycled first (one rune gives [1], then its
   Power), then base order.

The one real choice is the domain when runes of more than one matching domain could pay (dual-domain cards like
Death Mark, any-domain costs): a `Recycle` decision, one `Pick` per candidate domain (the rune step 2 would take),
domains in non-decreasing order and only picks that keep the rest payable, so each set of runes has one path. One
candidate = no decision. Which ready runes get exhausted never matters to the rules, so it's never asked. ⚠️
`cards.json` gives only the number of Power symbols: they count as the card's domains (any domain for a colorless
card); a card printed otherwise sets its cost in its rules (5.3). Add abilities on other cards (gear) join with #24.

**Tasks: HOT FEPR (334)**

- `tasks` is a stack (back runs next; a sequence is pushed in reverse). The loop: run tasks until a decision opens,
  the game ends or none are left; then Finalize/Execute/Pass/Resolve on the chain; then priority or the next phase
  (335).
- A task that needs a choice opens a decision and returns. The answer continues it from data in the state (the
  decision, the task's or chain item's step counter), so the engine stops anywhere mid-resolution and resumes. No
  C++ call stack spans a decision, no coroutines.
- **Cleanups** (318–323): game actions set a `cleanup_due` flag; the loop runs a Cleanup before the next task. A
  chain item's resolution is one task, so no cleanup happens inside it (321). A cleanup that changed something
  re-arms the flag (322).

**Card hooks** (5.3 fills them, #24 dispatches)

One `CardRules` subclass per card, registered by name (`cards/<set>/<num>_<slug>.cpp`). The engine asks it; it holds
no game state. Shape (exact signatures come with #24):

```cpp
struct CardRules {
    virtual void triggers(const Event&, Ctx&, ObjRef self) const;        // triggered (382): queue matching abilities
    virtual std::span<const Activated> activated() const;                // activated (376): cost, timing, effect
    virtual void passive(LayerCtx&, ObjRef self) const;                  // passive (363): effects per layer (479)
    virtual bool replace(Event&, Ctx&, ObjRef self) const;               // replacement (367): rewrite or cancel
    virtual void modify_cost(Cost&, const PlayCtx&, ObjRef self) const;  // cost modifiers, own and others' (366.2)
    virtual bool forbids(const Act&, const GameState&, ObjRef self) const;  // "can't" (054: beats "can")
    virtual bool enters_ready(const GameState&, ObjRef self) const;      // 369.3
    virtual std::span<const TargetSpec> targets() const;                 // chosen while playing (353)
    virtual void resolve(Resolution&) const;                             // the effect, as a step machine
};
```

- A choice inside an effect: `resolve` asks for a decision and returns; the engine calls it again with the next step
  and the answer.
- **Delayed abilities** (390) don't live on a card: a list on the state, plain data (creator, rules + ability index,
  subject `ObjRef`, window).
- **Events:** each game action emits typed events (moved, played, damage dealt, conquered, …); triggers are
  collected and ordered by turn order (303.2.a).

**RNG and replay**

- `Rng` (xoshiro256\*\*, splitmix64 seeding, 32 bytes) is part of the state: shuffles, the battlefields, the first
  player and random recycle order all draw from it. Own `below()` and Fisher–Yates: std's distributions and
  `std::shuffle` differ between standard libraries. A test pins the stream.
- Seed + action list = replay, the bug-report format (#27). The list lives outside the state, so copies stay small.

**Vanilla mode and test cards**

- `GameOptions::vanilla`: stats only, card text ignored. For tests and the random games until 5.3. Otherwise
  `new_game()` refuses every card without an implementation, all named in one error (D21). Basic runes are built
  in (164).
- `tests/test_cards.hpp`: cards defined in code with exactly the stats a test needs, no `cards.json`;
  `simple_deck("A")` is a legal 56-card deck.

**As built (#21–#23).** How the engine works now: `skeleton/services/game/CLAUDE.md`. Decisions made on the way:

- Mulligan 117: one decision per player in turn order; set-aside cards are `Removed` for a moment, then recycled in
  random order. `concede(s, p)` works any time and is not an `Action`.
- Burn Out 432 lives inside `draw()`: trash recycled, the opponent +1, repeated while the deck stays empty; from the
  second burn out in a row the Victory Score wins at once (432.3.c), the first waits for a cleanup (323.1).
- Priority has no field: `passes` counts passes in a row, the holder is the newest item's controller plus that many
  in turn order (337.4, 340.4). With no chain, 335 gives the turn player priority (no Priority task).
- Legality (358) is checked up front: `legal_actions` offers only legal plays, so 358.5's undo never happens.
- Timing = the card's leading `[Action]`/`[Reaction]` keyword (`Card::timing`), read in vanilla mode too; the
  fixture keeps just that keyword. Granted timing (Quick-Draw) comes from card rules (5.3).
- Cleanup 323 runs in the rule's order with the combat/showdown steps as named placeholders for #25. The Ending phase
  is two tasks (ending step; Expiration = Ending Special Cleanup + next turn), so lethal damage kills before units
  heal.
- `GameState::showdown` exists for timing only; showdowns, focus (312.2.b, 346) and their priority come with #25.
- Measurements, vi vs zed, 1000 seeded games, 0 failures under ASan/UBSan, same results on the VM and the Pi: #22
  0.5 ms per game, always 71 turns (no scoring: the deck runs out, the empty trash burns out to 8); #23 3.1 ms, ~182
  turns (spells refill the trash, so each burn out recycles cards and scores only 1), 128 spells and 3 cards played
  onto a chain per game.

**As built (#24, abilities and layers).** The card hooks as sketched above, with these decisions:

- `CardRules` is reached through `Card::rules` (static object, nullptr = not implemented); vanilla mode makes
  `rules_of()` return nullptr everywhere. Abilities are an indexed `AbilitySpec` table per card (activated, triggered,
  replacement); chain items, delayed abilities, effects and use counts refer to them by source `ObjRef` + index.
- One generic `Choose` decision for every choice the rules add (targets, "may", optional costs, cards to discard or
  recycle as a cost, trigger order, choices while resolving): options in a fixed order, picks ascending. Trigger order
  is the one place where order matters, so it's one pick per placement.
- Finalizing and resolving are step machines in `ChainItem` (`step`, `sub`, `vars`); a resolving item stays on the
  chain (`resolving`) until it's done, cleanups wait for it (321), queued triggers too (402.2).
- The chain now runs before tasks, so phase triggers (Beginning, Main, Ending) resolve inside their phase; Expiration
  repeats after a chain (317.2.f).
- Game actions with events and replacements live in `actions.hpp`; `move_to` stays the raw zone change. Simultaneous
  kills: all replacements, then every `Die` (own Deathknell, still on the board, "alone" noted), then the moves, then
  `Died` for everyone else (383.2.c).
- Layers: each `traits()` call starts from printed values and re-applies until no passive adds anything new (478.2–
  478.3, each mod once per evaluation). Order: base might, keyword grants/losses (passives, then effects by
  timestamp), increases, "increased to" (481's dependency), decreases. Non-passive limits are snapshotted at creation
  (`give_might`). Nothing is cached: 5.4 measures first.
- Cost: base → `SetBase` mods → additional and optional costs → increases → floored discounts, then the rest (the
  order a player would pick, 356.4.e) → ≥ 0. Optional costs are offered only when the total stays payable (355.16).
- Fixed choices where the rules give one to a player, ⚠️ until a card needs them: the order of several replacement
  effects on one event (372) and "may" replacements (371.2). Gaps that no deck card hits are listed in
  `services/game/CLAUDE.md`.
- Measurements, VM Release: vi vs zed vanilla 3.0 ms per game (#23: 2.4); 20 games of the ability test deck (every
  test card twice, both seats) 25 ms per game, ~150 activations, ~300 triggers and ~480 choices per game, all
  invariants after every action.

**As built (#25, movement, showdowns, combat, scoring).** Decisions:

- The Standard Move is a priority action per destination (`Move`, a battlefield or the base); which units go is a
  `Choose` (one or more, ascending), so several units move as one action (144.3) and stage one showdown. Units are
  played to the base or a battlefield their controller controls: a `Location` choice while finalizing (355.2).
- Contested is stored per battlefield (who applied it); staged showdowns and combats are derived from it every
  cleanup (323.7–323.10), not stored. One staged → it begins in the cleanup; several → a `Stage` task for the turn
  player's pick (323.11–323.12).
- `GameState::showdown` holds the showdown or combat in progress: focus, passes in sequence, attacker, the damage
  assignment. While its window is open the player with focus gets priority; a chain opened by a play or activation
  passes focus when it closes, one opened by triggers or an Add ability doesn't (346.1). All passed → the rest runs
  as tasks after each chain (ShowdownEnd; CombatDamage + Combat Special Cleanup, CombatResult, CombatEnd), which is
  how 468's "resolve the chain before the next step" falls out of the loop.
- Combat damage is a sequence of `Assign` picks, one unit each: lethal in full before the next, the last one gets
  what's left (467.2.c.3–4), Tank first (815). Only the set of lethal units and the one left short change the
  outcome, so lethal picks go in ascending order (Tank units first) and only picks that can't get stuck are offered:
  one path per outcome, as for every other set choice. Unassignable damage is the player's excess (Vi's legend,
  Yeti Brawler, Trapping Grounds read it from the `Conquer` event).
- Assault and Shield are layer-4 increases while the unit holds the designation (807.1.c, 814.1.c), so every Might
  check during a combat sees them; Ganking widens the Standard Move. Stunned units add no combat damage (424.1.b).
- Measurements, vi vs zed vanilla, 1000 seeded games, 0 failures under ASan/UBSan: every game ends on points by
  scoring, 17 turns on average (11–27; burning out takes 70+), 14 moves, 7 showdowns, 3.6 combats per game; VM Release
  0.58 ms per game. Zed wins 68 % (the mirror is 47/53: deck strength in vanilla, not the seat).

**As built (#26, game actions and the decks' keywords).** Decisions:

- **Vanilla mode stays stats-only.** Keywords come from `CardRules::keywords()` alone; parsing `[Assault 2]` out of the
  text would be a second source and gets dependent keywords wrong (Shadow Fiend's `[Empowered][>] I have [Assault 3]`).
- Keywords that need nothing but the keyword work from `keywords()`: Accelerate (an optional cost the engine adds,
  [1] + Power of the unit's domain), Ambush (Reaction timing and the extra locations), Deflect, Vision (an engine
  trigger, `ability_vision`, that predicts). Keyword abilities with a card-specific cost are an `AbilitySpec` with
  `builtin` (Empower, Equip): the card gives the cost, the engine resolves. Repeat and Flow are `CardRules` hooks
  (`repeat_costs()`, `flow_cost()`).
- Choices follow 355's order now: optional costs, location, targets. A cost can depend on targets (Deflect, Hextech
  Gauntlets), so `completable()` checks that the remaining targets can be chosen and the total paid before offering a
  play, an activation, an optional cost or a target (355.16); one look-ahead per slot, cheap for the decks' single-
  target cards.
- `Cost::any_power` = [A] (Deflect, Flow, Equip, Gold); payment takes domain Power first, then [A] from any rune. The
  Recycle choice stays one pick per domain.
- Tokens are engine cards (`tokens.cpp`: Bird, Shadow Clone, Gold), not cards.json entries (Riftcodex has only some
  tokens and the effect defines them, 187). Played tokens go through finalization like any card (location choice,
  Played event); one leaving the board becomes `Removed` for good (186.1), so the object list only grows.
- Attachment is `Object::attached`; `move_to` carries attached gear along and detaches it when its unit leaves the
  board, so every way a unit moves is covered. An attached card's rules are inactive (`active_rules`). The Might Bonus
  (137) is `CardRules::might_bonus()`: Riftcodex doesn't have it.
- Not built (no deck card needs it): reveal, counter, Equipment effect text, Equipped events, spending buffs.
- Measurements: vi vs zed vanilla, 1000 seeded games, 0 failures under ASan/UBSan, same game statistics as #25; VM
  Release 0.63 ms per game (#25: 0.58). Random games over the keyword and cost test decks (40 each) and the ability
  deck pass every invariant.

**As built (#27, hidden information, replay, the gate).** 5.2 is done. Decisions:

- **Knowledge per object**, not per zone: `Object::known` = a bit per player who knows which card it is (128).
  `move_to` sets it (public zone: everyone; hand: + owner; facedown: + controller; decks and set aside: unchanged),
  predicting adds the predictor (817), recycling two or more clears it (417.5: random order). So a card returned to
  Dev's hand stays known to Buddy, and one recycled alone stays known at the bottom of the deck.
- `view(s, p)`: every card p doesn't know becomes `hidden_card()`, the RNG is reset. Ids carry no information:
  setup shuffles the cards before it creates the deck objects, so an id is a deck position, not a card (the
  `table` service still tracks physical cards by id). A view isn't playable; search plays on `determinize(view, seat0,
  seat1, rng)`: per owner the deck's cards no visible object holds, at random (rune deck: runes), RNG reseeded.
  Tests check along random games (real decks vanilla, spell decks, ability deck) that a determinization passes every
  invariant, has the same view, gives the deciding player the **same legal actions** (nothing they can do depends
  on hidden cards) and plays on.
- Replay = seed + both decklists' text + the action list, as text (`replay.hpp`, one action per line, annotated
  with turn, player and `describe()`); `buddy-game --replay <file>` plays it with the random games' checks
  (`GameChecks`). A failing game in `buddy-game --random` or the gate writes its replay. Self-contained on purpose: a
  fixed decklist file doesn't change old replays.
- Gate: `RiftboundGate.ThousandRandomGames` (vi vs zed vanilla, seeds 0–999). All game tests carry the ctest label
  `riftbound`; `deploy/test.sh` skips them, `--riftbound` runs them (also `--pi`).
- Measurements: gate under ASan/UBSan 26 s on the VM, 32 s on the Pi, 0 failures. Pi Release, 1000 games: 0.72 ms
  per game (1390 games/s, 146k actions/s), 17 turns, 105 actions; `sizeof(GameState)` 896 B, ~112 objects, a copy
  0.83 µs (average over one game's states). The baseline for 5.4: ~1.2 M state copies per second per core before any
  fixed-capacity containers.

## Hardware notes for Phase 4 (decide there, not now)

- **Scanning:** the code is ~1.5 mm text. The head camera (4.2) won't read that at table distance, so it needs a fixed
  close-up camera at a scan spot. The Pi 5 has two camera ports, so a second camera fits. ⚠️
- **Hand tray:** keep the hand pile face-down on a clear tray with that camera underneath. The bottom card faces the
  camera, so "scan to confirm" costs no movement, and Dev still can't see Buddy's hand.
- **Taking the bottom card** is a hard grasp for a hobby-servo arm. Card dealer machines do exactly "eject the bottom
  card" with one friction roller. A roller-fed tray, with the arm only placing cards, is probably far easier than
  pinching a card off the bottom of a pile. The pile ops above fit either design.
- **Dev's cards:** for now Dev taps them on the panel. Reading Dev's cards on the table with the camera is an optional
  extra later.

## Open for Dev

- **Equipment Might Bonus** (for 5.3): Riftcodex has no Might Bonus. Read it off Serrated Dirk and Hextech Gauntlets
  (lower right corner, "+N").
- **When to slot Phase 5** relative to 2.x and 3: the software can run in parallel; the hardware waits for 4.1 and 4.2.
