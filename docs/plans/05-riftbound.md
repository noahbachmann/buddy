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
| Rules version | Core Rules 1.4 ("Vendetta"), from the official Rules Hub (playriftbound.com/en-us/rules-hub). The PDF stays out of Git, and tests cite rule numbers. |
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
- `legalActions(state, player)` and `apply(state, action)`. Mid-resolution choices ("choose a unit") are actions too,
  so the AI, the panel and the tests share one loop.
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
  article (it misspells "Carion Dredger"), chosen champion Vi, Hotheaded per Magical Meta ⚠️; Zed =
  [Piltover Archive](https://piltoverarchive.com/decks/view/f1d209f6-d427-491e-a958-90291b357198) and Magical Meta, identical.
- **Commands:** `deploy/run.sh game --check` (all decks), `--deck zed-vendetta --check`, `--code "OGN • 007a/298"`.
  `deploy.sh game` installs the binary and decks without a unit until 5.6.
- **Tests:** `services/game/tests/data/cards.json` = every printing of the two decks' cards (text removed); edge
  cases (conflicts, bad entries) inline in `test_card_db.cpp`.

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

- **Decklists:** `skeleton/services/game/decks/vi-unleashed.txt` and `zed-vendetta.txt` (sources in "5.1 as built").
  Check them against the boxes: `deploy/run.sh game --check` lists every card with its codes, and
  `deploy/run.sh game --code "<code from the card>"` names one card. Also: which Vi the box marks as the chosen
  champion (the list says Vi, Hotheaded).
- **When to slot Phase 5** relative to 2.x and 3: the software can run in parallel; the hardware waits for 4.1 and 4.2.
