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
- **v2 ISMCTS:** Monte Carlo tree search over legal moves, re-sampling Dev's hidden cards per iteration, truncated
  playouts scored by v1's evaluation, with a time budget per decision (start: 2 s on 4 threads on the Pi 5 ⚠️).
- How: "5.4 design" below. The AI sees only `view(s, p)` and both decklists: Buddy knows Dev's list (Dev, 2026-11-01:
  always an unmodified deck; which one comes with `new`, so a new deck needs no AI change).

### MQTT (`shared/topics.json`)

| Topic | Direction | Notes |
|---|---|---|
| `buddy/game/cmd` | → game | `new {buddy: zed\|vi, seed?, dev: human\|greedy\|random}`, `act {v, i}` (one of Dev's options), `reveal {v, cards}` (Dev's answer to a prompt), `resume`, `concede` (5.6 design) |
| `buddy/game/state` | game → | retained, built from `public_view` only (cards everyone knows): board, scores, Buddy's status, Dev's options or prompt, log, card catalogue. Never Buddy's hand. |
| `buddy/table/cmd` | game → table | `setup`, `deal`, `peek`/`peek_done`, `play {id, to}`, `move`, `exhaust`, `token`, `remove`, `rescan_hand`, … with a `seq` (5.5 design). `"web": false`: it moves the arm later (D19). |
| `buddy/table/event` | table → game | `done {seq, cards}`, `failed {seq, reason}`, `hand_corrected {hand, replaced, extra, missing}` |
| `buddy/table/debug` | table → | retained belief (debug; not `/state`: web would stream Buddy's hand to the browser) |

**Dev's moves** come from a Riftbound page on the web panel: the board, Dev's legal actions as buttons, and pass/respond
prompts during showdowns and the chain. Dev plays real cards (5.6 design): the page asks for a card when it turns public. Voice for common
moves ("pass", "I attack the left battlefield") comes after that.

## Steps

| Step | Content | Done when |
|---|---|---|
| 5.1 Card data & decks | `deploy/pi/fetch-cards.sh` → `~/buddy/data/riftbound/cards.json` (pruned, not in Git). Code parser, printing → card map, decklist format (Piltover Archive text). Both decklists in `services/game/decks/`; a small card fixture for tests in the repo. | `buddy-game --deck <file> --check` lists every card; an unknown code is a clear error |
| 5.2 Rules engine core | Turn structure, runes/energy/power, units, moves, battlefields, showdowns, combat, chain/priority, conquer/hold scoring and the win condition, mulligan. Keywords only as the two decks need them. gtest per rules section. | 1000 seeded random-vs-random games without assertion failures under ASan/UBSan |
| 5.3 Cards of both decks | Every card in both decklists, one test each. Dev reviews odd rulings. | both decks load, per-card tests green |
| 5.4 AI player | v1 heuristic, then v2 ISMCTS | v1 beats random ≥ 180 / 200; v2 beats v1 ≥ 115 / 200 (95 % Wilson lower bound > 50 %) at a fixed iteration budget; decision time on the Pi p95 ≤ budget + 10 % |
| 5.5 `table` service + sim ✅ | piles, primitive ops, hand tracker, verify/recover, fault injection. Property tests: random op sequences with faults always end in the correct state. | 10k games' worth of pile ops with faults: zero unrecovered states; scans per play reported (done 2026-11-04, #45: "5.5 as built") |
| 5.6 `game` service + panel page | session (table acts before the state commits, scanned cards bound into the engine), Dev's real cards, MQTT, Riftbound page, voice lines via `buddy/voice/say`, `buddy-game --sim` (#46) | the phase's "Done when" above, with Dev's real cards; before it the session soak: 1000 games under stress, 0 unrecovered, 0 desyncs, 0 leaks |
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

### 5.3 as built (2026-11-01, #33)

- **Cards:** `services/game/riftbound/cards/`: `vi_unleashed.cpp` (28), `zed_vendetta.cpp` (24), one `CardRules`
  per card with its text above it; basic runes need none. `CardDb::parse` attaches them by name, so a decklist with
  an unwritten card still fails at `new_game` (D21). `buddy-game --check` has a "rules" column, `--random --rules`
  plays with them. Sub-issues: #28 simple units, #29 spells and gear, #30 units with triggers and abilities, #31
  legends and battlefields, #32 gate and docs.
- **Engine changes the cards needed:** Equipment effect keywords (`effect_keywords()`, 718.3: Serrated Dirk);
  a tax on the Standard Move (`move_tax()`, Mageseeker Investigator: the Move choice is capped at what's payable);
  a permanent's Played event carries its paid optional costs ("if you paid the additional cost"); battlefield ability
  control (190.6: the battlefield's controller, else the turn player; `AbilitySpec::event_player` for "they may …"
  and "when you conquer here"); location slots picked while playing (`TargetSpec::location`: Twilight Step). Bug fixed: Awaken now readies the legend (315.1.b).
- **Tests:** `tests/test_cards_vi.cpp`, `test_cards_zed.cpp`: one test per card (several branches where the text
  has an "if"), on the text-free fixture plus the rules from `cards/`, and 40 random games of every registered card
  per deck file. Gate: `RiftboundGate.ThousandRandomGamesWithRules` next to the vanilla one; hidden-information views
  along rules-on games of both decks.
- **Measurements:** gate with rules under ASan/UBSan 73 s on the VM, 0 failures; all 592 tests green on the Pi. Pi
  Release, 1000 games, seeds 0–999: **2.97 ms per game** with rules (336 games/s, 56k actions/s, 167 actions, 17
  turns, 6.5 spells and 0.8 chain plays per game, 500:500 wins) vs 0.74 ms vanilla (142k actions/s, 343:657). A
  state copy stays 0.85 µs; an action costs ~2.5× vanilla's: `traits()` asks every object's passives each call.
  The first thing to profile when 5.4's search wants more playouts.

#### Rulings (all confirmed by Dev, 2026-11-01)

Where the card text or the Core Rules leave room, this is what the engine does. A different reading is a small change
in the card's file. "AI:" = how Dev plays it, for the search (#37), not a rule.

| Card | Ruling |
|---|---|
| Twilight Step | ✅ Dev, 2026-11-01: the destination is picked as it's played, after the unit (any location it isn't at: a battlefield, or its controller's base); not targeted. If the unit is there by the time it resolves, nothing moves. |
| Death Mark, Zed From the Shadows, Trapping Grounds | ✅ Dev, 2026-11-01. Token without a stated place: a location choice like any played unit (184.2): base or a battlefield its player controls. |
| Square Up, Upstage Comedy | ✅ Dev, 2026-11-01. Each paid Repeat chooses its own target (820.2); Square Up twice on one unit = Assault 8 (807.2). |
| Serrated Dirk | ✅ Dev, 2026-11-01. [Assault] = Assault 1 on the equipped unit, Might Bonus +0 (as printed). |
| Mageseeker Investigator | ✅ Dev, 2026-11-01. "My battlefield" = the battlefield it's at. Only Standard Moves move several units at once. The [A] runes are recycled in the engine's order, no choice (no Recycle decision outside the chain). |
| Zed, Without a Sound | ✅ Dev, 2026-11-01. Zed and the clone at the same location: nothing moves (no Moved events, not a move). Otherwise two moves, Zed first. AI: at the same location it's a wasted play, never worth it. |
| Gust Monk | ✅ Dev, 2026-11-01. Unit (target) and card (picked, either trash) are both chosen as the trigger goes on the chain; no card, or it left the trash before the trigger resolves → no Assault. |
| Morgana, Vindictive | ✅ Dev, 2026-11-01. Damage = what's marked on the target as the trigger resolves. |
| Lord Broadmane | ✅ Dev, 2026-11-01. "Here" = where it entered (its base too). |
| Vi, Peacekeeper; Twilight Reveler | ✅ Dev, 2026-11-01. Targets chosen as the attack trigger goes on the chain. |
| Blade Twirler | ✅ Dev, 2026-11-01. "Choose a player" = target either player. |
| Ravenbloom Prefect | ✅ Dev, 2026-11-01. The gear is banished only if it's still on the board. |
| Vi, Piltover Enforcer | ✅ Dev, 2026-11-01. "A unit" = any unit, either side. Triggers while exhausted too, but then can't pay. AI: rarely worth using on an enemy unit. |
| Battlefields (190.6) | ✅ Dev, 2026-11-01. "When you conquer/hold here" and "a player plays … they may" belong to the player who did it; Threshold of the Gray to the battlefield's controller (no choices in it). |
| Star Spring | ✅ Dev, 2026-11-01: once per turn in total, not per player; the other unit is a target of the player who played. |
| Valley of Idols | ✅ Dev, 2026-11-01. Tokens played there trigger it ("a unit"). |

### 5.4 design (2026-11-01, #39)

Sub-issues, in order: #34 player API and arena, #35 evaluation and v1 greedy, #36 engine speed, #37 v2 ISMCTS, #38 Pi
budget, tuning, docs. Code in `services/game/riftbound/ai/` (pure, like the engine), `buddy-game --match`.

**Why truncated playouts:** with rules the engine does 56k actions/s per core on the Pi (5.3) and a game is ~167
actions, so full random playouts give ~330 per second per core: ~2600 in 2 s on 4 cores. Too few for plain MCTS over
this branching. v2 plays each playout only a few actions (or to the end of the turn) and scores it with v1's `eval`,
and the engine gets its own speed step (#36) before the search.

| Topic | Decision |
|---|---|
| What the AI sees | `Player::choose(view, seat0, seat1)`: only `view(s, p)` and both decklists, never the referee's state. The arena hands each seat its view, so a leak is impossible by construction. Search plays on `determinize(view, …)`, which gives the deciding player the same legal actions in every determinization (#27). |
| Forced moves | A decision with one legal action is applied without asking the player, in the arena and inside the search. |
| Action keys | Hand and trash cards by card (two Zaps = one move; an opponent's hidden hand card is the same move in every determinization), board objects by `ObjId`, plus kind and `n`. |
| Evaluation | `eval(s, p)`, shared by v1 and v2: points against `victory_score` (steeper near 8), battlefield control and contests, might per battlefield, units, cards in hand, runes ready next turn, legend; a win or loss is ±∞. Weights in one struct: hand-set, then tuned by self-play (#38). |
| v1 greedy | On one determinization, per legal action: apply, play on to quiescence (own sub-decisions greedy, the opponent passes and declines every choice, chain resolved), `eval`. Best wins, ties by action order (deterministic). Mulligan by a rule (cost threshold), not search. |
| v2 ISMCTS | Single observer: one tree over action keys, a fresh determinization per iteration, UCB1 over the actions available in that determinization (availability counts), one expansion, truncated playout, `eval` squashed to [0, 1]. Budget in iterations (tests, arena: reproducible) or time (Pi). Root parallelization: a tree, Rng and determinizations per thread, root visits summed, no locks. Accepted weakness: in the tree the opponent plays as if it saw Buddy's hand (strategy fusion); MO-ISMCTS only if the numbers ask for it. |
| Measurement | `buddy-game --match <a> <b> [--games N] [--seed S] [--iterations N \| --budget-ms M] [--threads T] [--jobs J]`, players `random`, `greedy`, `ismcts`. Each pairing plays both decks from both seats, games in parallel. Reports wins with the 95 % Wilson interval, per deck, decision time p50/p95/max, iterations per decision. `GameChecks` after every action; a failure writes a replay. |
| Pass marks | "> 50 % of 200 games" is noise, so: v1 vs random ≥ 180 / 200, v2 vs v1 ≥ 115 / 200 (Wilson lower bound > 50 %) at a fixed iteration budget on the VM. The Pi measures time, not strength. |

Later, not 5.4: thinking during Dev's turn, reusing the tree between decisions, MO-ISMCTS. The hand pile's order never
weighs on the choice (Dev, 2026-11-01): play strength only, feeds are the table's cost.

**As built (#34–#38, 2026-11-01).** Code in `services/game/riftbound/ai/`, how it runs: `services/game/CLAUDE.md`.

- **Arena:** `Player::choose(view, actions)` gets `view(s, seat)` and the legal actions (the same in every
  determinization); a test checks every state a player gets is a view of its own seat. `play_match`: seed S + i/2,
  every seed twice with the players swapped (same shuffles), decks swap seats every other seed; per-game player
  seeds, so results don't depend on `--jobs` (and VM and Pi play the same games).
- **Greedy:** quiescence = anyone's Neutral Open priority or game over, so a move's showdown or combat and a spell's
  chain play out before `eval`. EndTurn is scored as the state now: playing on into the opponent's turn would charge
  it alone with their Hold, draw and channel. Own sub-decisions nested greedy (depth 2), then pass/decline/first.
- **Branching** (random vs random, 1000 games): 172 decisions per game, 41 % forced; legal actions p50 2, p95 8, max
  37; Choose runs after a Priority action p50 1, p95 3, max 9.
- **Results** (Release, seed 0, both decks from both seats): random vs random 479:521 (CI 44.8–51.0 %), decks
  vi:zed 517:483 (`--random` 500:500). **Greedy vs random 190 / 200** (CI 91.0–97.3 %; seed 1000: 196), 14.6 turns.
  Greedy vs greedy: Zed wins 146 / 200: with sensible play the Zed precon is the stronger deck ⚠️ (watch it in v2).
- **Pi** (Release, one job): greedy decision p50 0.24 ms, p95 3.4 ms, max 30 ms, 41 decisions asked per game.
- Pass mark detail: 114 / 200 already has a Wilson lower bound > 50 % (50.07 %); the 115 above keeps one game spare.
- **Engine speed (#36).** `perf` (VM, Release + frame pointers; the Pi has no perf) showed every object loop paying a
  card-pointer chase and a virtual `active_in()` per object (36 % in `active_rules` alone), and `apply` rebuilding
  `legal_actions` to validate one action (half of all `legal_actions` time). Fixes, in order: `active_rules` checks
  the object's zone first (the unused `active_in` hook is gone); `apply` checks only the given action (`is_legal`;
  a test probes every Play/Activate/Move/Pass along rules-on games against `legal_actions`); `GameState::active`,
  the ascending ids in board and legend zones kept by `move_to`/`create` (checked by `check_invariants`), walked by
  `traits`, `total_cost`, `emit`, `replaced`, `move_tax`, the activation and mover loops and `cleanup`. No `traits()`
  cache: after these it is ~10 % of the time, not worth an invalidation scheme. Same games: `--random` prints a
  digest over every game's winner, turns and actions, unchanged (rules `3fa44f83335b8657`, vanilla
  `d04c2a02e1c41238`, VM = Pi); greedy vs random still 190 / 200. Pi Release, seeds 0–999, with rules: **55k →
  143k actions/s** with `GameChecks` (2.6×), **64k → 244k** engine only (`legal_actions` + `apply`, what a playout
  pays: 3.8×, 4.1 µs per action); vanilla 144k → 196k / 224k → 448k. State copy 0.93 µs (920 B). Full random
  playouts would now be ~1460 per second per core; truncated playouts stay the plan.
- **v2 ISMCTS (#37)** as designed above (`ai/ismcts`). Details that weren't in the design: forced moves get no node
  (as in the arena); the root's edges are the given actions merged by key **on the view**, applied as they are in
  every determinization (an opponent's hidden card would key differently per determinization), deeper nodes key on
  the determinization; the playout plays `playout` random actions within the turn, then passes and declines until
  someone has Neutral Open priority (eval never reads a half-resolved chain); reward = 1 / (1 + e^(−eval / 20)),
  win 1, loss 0; most root visits wins, ties by action order. An iteration budget is split over the threads (fixed
  shares: reproducible for a given thread count). ~55 µs per iteration on one core, VM and Pi alike.
- **Tuning (#38)**, ISMCTS vs greedy, 1000 iterations, one thread. Seeds 0–99 (200 games): c 0.7 / playout 20
  (first version) 149; c 0.4: 157, c 1.0: 146; playout 0: 160, 10: 164, 40: 152; scale 10: 147, 40: 142 (20 stays).
  Combined c 0.4 + playout 10: 160; on top, eval weights hand 3: 166, control 8: 167, gear 3: 167, rune 3: 157,
  might_field 1.5: 158. Every single step is inside the noise (CI ± 6 %), so the candidates went to fresh seeds
  1000–1199 (400 games): c 0.7 / playout 20 **293** (73.2 %), c 0.4 / playout 10 **325** (81.2 %, CI 77.1–84.8 %),
  plus hand 3, control 8, gear 3: 308. Kept: c 0.4, playout 10, the hand-set weights (greedy shares them).
  `--tune` takes these keys for the next round.
- **#37's pass mark:** ISMCTS (1000 iterations) vs greedy **160 / 200** (seeds 0–99, CI 73.9–85.0 %; ≥ 115 needed).
  More iterations keep paying: 8000 iterations **184 / 200** (92 %, CI 87.4–95.0 %; 14.5 turns instead of 15.8).
- **Pi (#38)**, 2 s budget, Buddy's services running, vs greedy, 8 games each (Release, one job): 4 threads
  p50 92k iterations per decision (p5 68k), time p95 2003.9 ms, max 2005.7 ms; 3 threads p50 71k (p5 51k), p95
  2002.3 ms, max 2002.8 ms; both with the first version's c 0.7 / playout 20, 16 / 16 won. With the tuned defaults,
  3 threads: p50 78k (p5 53k, shorter playouts), p95 2002.5 ms, max 2003.1 ms, 7 / 8 won. All within budget + 10 %
  (2.2 s). Dev used the voice during the
  3-thread run: wake word and speech-to-text (1.1 s) as usual, no xruns or watchdog hits. **Default for the service
  (5.6): 3 threads, 2 s**: the fourth core stays for ears, which must hear "Hey Jarvis" while Buddy thinks; 3 threads
  lose only a quarter of the iterations. Whether 70k beats 8000 by as much as 8000 beats 1000 isn't measured (one
  match takes hours on the VM); 5.6 may still shorten the budget if waiting 2 s per decision feels slow. ⚠️
- Tests (`test_ai`): reproducible under an iteration budget (1 and 3 threads), takes the win in 1, ≥ 1000 searched
  decisions in ISMCTS-vs-ISMCTS games all legal. 5.4's checks green on the Pi (`deploy/test.sh --pi --riftbound`:
  611 tests).

### 5.5 design (2026-11-04, #45)

Sub-issues, in order: #40 protocol, pile model, tracker; #41 sim backend and faults; #42 engine moves → table
commands; #43 soak and defaults (the gate); #44 service, deploy, docs. Code in `services/table/` (`table_core`,
`buddy-table`), the protocol in `shared/table_protocol.*`, the engine side in `services/game/table_moves.*`
(`game_core`).

**Facts from the engine:** the hand list is the pile model (`draw` takes `deck.back()` = top and appends = top of
H, a card leaving keeps the others' order, a returned card is appended). Decklists name cards, not printings: the
code is known only after a scan, so the table works in codes and game maps code → card. Buddy's cards also leave
hidden piles through Burn, Vision, channel, mulligan, Burn Out, recycle from the trash, return to hand, discard costs
and tokens.

| Topic | Decision |
|---|---|
| Ownership | table owns every physical card of Buddy's (piles, board spots, exhausted or not); game owns the rules and which object is which card. Dev's cards are never tracked. Battlefield cards don't move: not tracked. |
| Ids | The table numbers each card it deals or sets up (`id`) and reports `{id, code}`. A card going back into a deck loses its id (a later deal gives a new one). Same-code copies in the hand are interchangeable: a play takes the nearest copy and keeps the asked id (the ids swap in the belief). |
| Zones | Piles (hidden, ordered bottom → top): `deck`, `runes`, two hand trays (H and B, the names swap after every merge) and the scan **spot** (deals and takes land there, a placing arm moves its bottom card on). Spots (face up, cards by id): `base`, `bf0`/`bf1`, `fd0`/`fd1` (facedown), `chain`, `trash`, `banish`, `champion`, `legend`. Places a card can go: those spots, `hand` (top of H), `deck_top`, `deck_bottom`, `runes_bottom`. |
| Backend | `scan(pile)` → code / unreadable / empty (bottom card), `feed(H → B)`, `take(H → spot)`, `deal(deck\|runes → spot)`, `stack(H onto B)`, `place(spot → place)`, `put(id → place)`, `turn(id, exhausted)`, `shuffle_trash_into_deck`, `token`, `remove`. Only scans, feeds, takes and deals fail; placing a held card and moving a card on a spot don't (sim). |
| Reads | `confirm_reads` agreeing reads of the same pile give a code (an unreadable read doesn't count, a different code starts over; a bounded number of tries, then `unreadable`). Deals and plays read this way. |
| Post-check | After placing the spot's card, scan the spot: it must be empty. A card left there came along by a double deal (→ back on the deck top) or a double take (→ top of B, which is exactly its place in the merged hand). Scanning the spot instead of the next hand card (the issue's first idea) also catches a double take of two same-code cards. |
| Play X | nearest copy of X at index k: k × `feed(H → B)`, confirm X at the bottom of H, `take`, `place`, post-check, `stack(H onto B)`. Mismatch → recover, retry once, else `failed not_in_hand`. |
| Recover | Full scan passes H → B (each card read, then fed). A pass is accepted if it matches the believed multiset (adopts the order: a disturbed pile), else passes until two agree (max `max_passes` → `failed hardware`). The believed ids follow the scanned codes: same multiset → by code in order; otherwise an edit-distance alignment keeps ids where codes match, pairs the rest as `replaced` (a misread at draw), leftovers are `extra` (new id) or `missing`. `hand_corrected {hand, replaced, extra, missing}`; game decides (extra → back on the deck top). |
| Sim faults | Seeded, per op: misread (a wrong code from the sim's codes), unreadable, double feed, missed feed, double take, double deal; per command: pile disturbed (two hand cards swap). |
| Unrecovered | A wrong card played, `failed hardware`, or belief ≠ truth after a recovery or at game end (after a final `rescan_hand`). |
| Engine → commands | The diff of two states can't order a Burn Out between two draws or tell which recycled card went down first, so the engine reports its physical moves: a thread-local move log in `move_to`/`create`, installed only around the referee's `new_game`/`apply` (search never logs). `physical_moves(before, after, log, seat)` turns one action's log into table commands: hops through the chain or "set aside" collapse (hand → chain → base = one play), consecutive deck → same place = one `deal`, deck → deck bottom = `peek` + `peek_done`, exhaust changes from the before/after diff. 5.6 uses it as is. |
| Workload | `buddy-game --table-workload --games N --seed S [greedy\|random]` → JSON lines: per game a `setup` (Buddy's seat, deck and rune order as home-printing codes with the engine objects), then commands with engine object ids (`deal` lists the objects it deals). `buddy-table --soak -` stands in for game. |

Open for 5.6: whether game sends a play before or after `apply` and what happens on `failed`; the engine taking drawn
codes in place of its own deck order; Vision `peek`s before the choice. Hardware details wait for 5.7+.

### 5.5 as built (2026-11-04, #40–#44)

Code: `shared/table_protocol.*`, `services/table/` (`table_core`: `tracker`, `table`, `sim`, `soak`; `buddy-table`),
`services/game/table_moves.*` (`game_core`). How it runs: `services/table/CLAUDE.md`.

What the tests changed against the design above:

- **Reads vote.** A code needs `confirm_reads` reads of it, not in a row: a misread between two good reads doesn't
  start over. A dealt card that stays unreadable goes back on top (`failed unreadable`), not left on the spot.
- **Recovery passes go through the spot**, not blind feeds: take, read, place on B, and read whatever a double take
  brought along. Blind feeds skip a card on a double feed, and two passes then agreed on a hand with a card missing.
- **After a mismatch** the play is retried by searching card by card on the spot (every card read), not by blind
  feeds again: under 5 % double feeds, two fast attempts in a row failed often in big hands.
- **Same-code copies:** the table plays exactly the id it's asked for; game picks the copy nearest the bottom
  (`nearest_copy`, the AI keys hand cards by card anyway). With the table choosing, the engine removed the other copy
  and its hand order stopped being the pile's.
- **A double feed onto a same-code copy** further up plays that copy: same codes, the order drifts by one; the next
  recovery (at the latest the final rescan) adopts it. Accepted: no extra scan per play for it.
- **Engine → table** reports moves through a thread-local log (`riftbound::move_log`), see the table above; the
  `--random --rules` digest over seeds 0–999 is unchanged (`42705b80131137f7`).

**Soak (#43):** `buddy-game --table-workload --games 10000 --seed 0` (greedy players, both decks from both seats,
824k commands, 66 s on the VM in Release) `| buddy-table --soak -`. 13.3 plays and 32.9 deals per game.

| Run | Unrecovered | Feeds per play p50 / p95 / max | Scans per play p50 / p95 / max | Scans per game |
|---|---|---|---|---|
| no faults | 0 | 1 / 4 / 7 | 4 / 4 / 4 | 191 |
| stress, `confirm_reads` 1 | 3239 | 1 / 4 / 10 | 2 / 18 / 105 | 123 |
| stress, `confirm_reads` 2 | 11 | 1 / 4 / 7 | 3 / 22 / 58 | 174 |
| **stress, `confirm_reads` 3 (default)** | **0** | 1 / 4 / 7 | 4 / 30 / 64 | 233 |
| stress, 2, post-check off | 20 934 | 1 / 4 / 8 | 2 / 24 / 117 | 134 |
| stress, 3, post-check off | 21 124 | 1 / 4 / 8 | 3 / 33 / 132 | 196 |

Stress rates ⚠️ (guesses until 5.7 measures a scanner): misread 2 %, unreadable 5 %, double/missed feed 2 %,
double take 1 %, double deal 1 % per op, disturbed 1 % per command. At `confirm_reads` 3 under stress: 2.1
recoveries per game (max 7, the final rescan included), 4825 corrections in 10k games (all order fixes after a
disturbed pile), no `failed`. Sim seeds 1–3: 0 unrecovered as well.

- `confirm_reads` 2's 11: misreads that agreed twice on cards nobody scans again (a rune into the base, a burned
  card into the trash: "base/trash differs" at game end) or on a drawn card whose wrong code matched another hand
  card (that one got played). 1 adds every single misread. **Default 3**: a third of the scans more for none left.
- Post-check off: a double take or deal leaves a card on the spot, the next deal or take reads that one instead.
  **Default on**: it costs one scan per deal and play.
- ctest (label `riftbound`): `RiftboundTableMoves.SoakUnderStress`, 200 random-player games under the stress rates
  (16 s under ASan).

**Service (#44):** `buddy@table`, sim backend only (`BUDDY_TABLE_*`: faults, seed, `confirm_reads`, post-check).
`setup` may carry `sim_deck`/`sim_runes` (game's decklist as home-printing codes; without them the sim uses `SIM-n`
placeholders); the sim shuffles them. The belief goes retained on **`buddy/table/debug`, not `buddy/table/state`**:
web streams every retained `buddy/+/state` to the browser, and this one holds Buddy's hand. On the Pi: `deal`/`play`
published by hand give `done` events and a consistent belief; the gate pipeline as written above (Release, `--jobs 3`)
gives the VM's numbers exactly (same games, same faults) in 2.5 min. `deploy/test.sh --pi --riftbound`: 643 tests green.

Open for 5.6 (unchanged from the design): whether game sends a play before or after `apply` and what happens on
`failed`; the engine taking drawn codes in place of its own deck order (a misread public card is then the engine's
problem too); Vision `peek`s before the choice (the workload only peeks when the card goes to the bottom).

### 5.6 design (2026-11-04, #46)

Sub-issues, in order: #47 engine: binding hidden cards, public view, replay lines; #48 session: step protocol and
Buddy's side; #49 Dev's real cards; #50 public state, commands, voice lines; #51 session soak and terminal play (the
gate); #52 service and deploy; #53 panel page, live game, docs. Code in `services/game/` (`session.*` and the rest in
`game_core`, `buddy-game --sim`, `buddy@game`), the page in `services/web/www/`.

**Dev plays real cards (Dev, 2026-11-04).** The referee doesn't know Dev's hand or deck order: Dev enters a card when
it turns public (played, discarded, burned, channelled, the battlefield at setup). Buddy's draws are the same problem:
the table reads codes, and the deck is shuffled by the sim (later by hand), not by the engine.

| Topic | Decision |
|---|---|
| Binding | `bind(s, obj, card)` swaps `Object::card` with an object of the same owner holding that card, from a pool of cards Buddy doesn't know (Buddy: deck and rune deck; Dev: hand, deck, rune deck). Zones, `gen`, `known` unchanged, so views and determinizations stay valid. A misread hand card is rebound the same way; a disturbed pile gets `set_hand_order`. Replays get `bind`/`order` lines: seed + actions + binds reproduce a service game. |
| Oracles | One per seat answers reveals: Buddy's = the table (`deal`/`peek` codes → cards), Dev's = a prompt on the panel ("which card did you burn?", "which runes did you channel?", "your battlefield?"). Tests and bot games: `SimDev`, a true shuffled deck that answers truthfully. |
| Step protocol | Answers 5.5's first open point. `next = s; apply(next, a)` under the move log, `TableMoves::step` gives the commands. Commands and prompts run one at a time in log order; each revealed identity is bound on `s` and `next` re-applied. Whatever already ran must be a prefix of the new list, else a **desync** (pause, replay written; the soak counts them, expected 0: no deck card branches on a drawn or burned card's identity within the step). After the last `done`, `s = next`. The table always acts before the state commits. |
| Vision | Peek as soon as the engine shows Buddy the top card (Vision's `predict` opens the decision): bound before the AI chooses, `peek_done` follows the answer. `TableMoves` changes for it (#47). |
| Failures | `not_in_hand` on Buddy's play (the step's first command): `hand_corrected` into `s`, extras back on the deck top, the AI decides again. `unreadable`: retry once, then pause. `hardware`, `missing`, a table restart: pause and say so; `resume` (after a `rescan_hand`) or `concede`. `replaced` → rebind + "I misread a card". |
| Dev's options | Dev's hidden objects hold placeholder cards, never shown or trusted. Plays: the non-hand actions plus one "play C" per card C that could be in Dev's hand (decklist minus the copies known elsewhere), each checked on a copy with a hand object bound to C; choosing C binds it for real. Choices that reveal a hand card (discard costs) go by card, the others (mulligan) by count. An impossible entry is refused. |
| Public state | `buddy/game/state` comes only from `public_view(s)` (cards known to everyone): free of Buddy's hand and of Dev's placeholders by construction. Leak test: the JSON is the same after re-determinizing Buddy's hidden cards or Dev's placeholders. The card catalogue goes in once; web's JSON patches carry only changes afterwards. |
| AI in the service | ISMCTS 2 s, 3 threads (5.4) on a worker thread; forced moves unasked; `nearest_copy` on Buddy's choice. |
| Voice | Game start, Buddy's plays/moves/attacks (one line per action group, never per pass), scoring, corrections, pauses, the result. |
| Gate | `buddy-game --sim --games 1000 --faults stress` (`SimDev` greedy vs Buddy greedy, both decks from both seats): 0 unrecovered, 0 desyncs, 0 leaks; 50 games vs ISMCTS (1000 iterations). Also reports Dev's prompts and taps per game: what real cards cost Dev ⚠️. |

Later, not 5.6: voice input ("pass", "let's play Riftbound": STT doesn't know these words yet, next voice session),
thinking during Dev's turn, a game that survives a *table* restart (the sim's piles are in memory; a *game* restart
resumes from the replay). **D19:** `buddy/game/cmd` is on the web without a login; fine while the table is a sim,
a login comes before the table drives motion (5.7).

## Hardware notes for Phase 4 (decide there, not now)

- **Scanning:** the code is ~1.5 mm text. The head camera (4.2) won't read that at table distance, so it needs a fixed
  close-up camera at a scan spot. The Pi 5 has two camera ports, so a second camera fits. ⚠️
- **Hand tray:** keep the hand pile face-down on a clear tray with that camera underneath. The bottom card faces the
  camera, so "scan to confirm" costs no movement, and Dev still can't see Buddy's hand.
- **Taking the bottom card** is a hard grasp for a hobby-servo arm. Card dealer machines do exactly "eject the bottom
  card" with one friction roller. A roller-fed tray, with the arm only placing cards, is probably far easier than
  pinching a card off the bottom of a pile. The pile ops above fit either design.
- **Dispenser (Dev's design, 2026-11-07):** hand pile H on a belt roller over the camera, B on a lift, both in one
  box: `parts/dispenser/` (roller done, feed into B still stops 15 mm short: README → Open). A pusher on a lead screw
  moves all of B back over the divider onto H: the hardware does **`stack(B onto H)`**, not `stack(H onto B)`, so the
  tracker must use the rotation variant above. ⚠️ Before 5.7: `Tracker::merge()` (table) stacks H onto B and swaps
  the tray names; with this hardware H stays on the roller and B goes on top of it, so merge and the sim must follow.
- **Dev's cards:** for now Dev taps them on the panel. Reading Dev's cards on the table with the camera is an optional
  extra later.

## Equipment as printed (Dev, 2026-11-01; for 5.3)

Riftcodex has neither the Might Bonus (137) nor the Effect Text (136.2: inactive until attached, then appended to the
unit's rules text, 718.3), only the rules text (the Equip line).

| Card | Might Bonus | Effect Text |
|---|---|---|
| Serrated Dirk | +0 | [Assault] (= Assault 1, 807.1.b.3): the unit it's attached to has Assault |
| Hextech Gauntlets | +3 | none (the only text is the Equip line Riftcodex has) |

Built in 5.3 (#29): `CardRules::might_bonus()` and `effect_keywords()`, which `traits()` adds to the unit while the
gear is attached.

## Open for Dev

- **5.3 rulings:** the table in "5.3 as built" against the physical cards.
- **When to slot Phase 5** relative to 2.x and 3: the software can run in parallel; the hardware waits for 4.1 and 4.2.
