# services/table – Buddy's physical Riftbound cards (5.5)

What table does **now** (after #45: sim backend, soak gate passed). Why it's built this way and the soak numbers:
`docs/plans/05-riftbound.md` → "5.5 design", "5.5 as built". Protocol: `shared/include/buddy/table_protocol.hpp`.
The engine side (which commands a game step needs): `services/game/table_moves.*`.

## Files (`table_core`: no MQTT)

| File | What |
|---|---|
| `backend.hpp` | The primitive ops, sim now, hardware 5.7+: `scan` (bottom card: code / unreadable / empty, empty is reliable), `feed` (H → B), `take` (H → spot), `deal` (deck top → spot), `stack`, `place` (the spot's bottom card → pile end or spot as an id), `put` (a card on a spot → pile or spot), `turn`, `shuffle_trash_into_deck`, `token`, `remove`. Piles: `Deck`, `Runes`, `TrayA`/`TrayB` (hand and buffer: the tracker swaps the names after every merge), `Spot` (the scan spot). `keeps_exhausted`: board → board moves keep the state, like the engine |
| `tracker` | `Belief` (hand bottom → top, spots by `Place`, deck/rune counts, a held peeked card) and the procedures: `read` (votes: `confirm_reads` reads of one code), `deal` (read, place, post-check: extras back on top), `play` (k feeds, confirm, take, place, post-check: an extra goes on B's top, merge; mismatch → merge, `recover`, `search` card by card through the spot), `recover` (passes through the spot until the multiset matches or two passes agree; `adopt`: by code, or an edit-distance alignment → `replaced`/`extra`/`missing`), `peek`/`peek_done` (Vision; a double deal under a card going back on top is fixed by dealing it again). Every procedure throws `Failure{reason}`; `OpCounts` cumulative |
| `table` | `Command` → tracker → events: `hand_corrected` first (seq filled in), then exactly one `done`/`failed`; any backend exception = `failed hardware`. `state()` = the belief as JSON (`buddy/table/debug`) |
| `sim` | `Sim`: true piles (`Phys` with a serial) and spots, seeded faults per op (`SimFaults`, `parse_faults`: `stress` = the 5.5 rates), `placed()` (cards the spot gave away this command, not onto the trays: in a play the first is the card that left the hand), `compare(belief, hand_pile, hand_only)` = "" if the belief is the truth. `configure()` per setup (the service: decks from the command or `SIM-n` placeholders, shuffled) |
| `soak` | `StandIn` plays one workload game for game: engine objects → table ids (`deal`/`token`/`peek` map them in order), extra cards back on the deck top, a failed play or deal retried once; counts unrecovered (wrong card played per the engine's code, `failed`, belief ≠ truth after a recovery or after the final rescan). `soak()` reads JSON lines, `print_report()` |
| `main.cpp` | Service (`BUDDY_TABLE_*`, sim only), `--cmd '<json>'…` (fresh sim, seq optional), `--soak <file\|->` |

## Tests

- `tests/test_tracker.cpp`: fault-free sim: every command, a play costs exactly its index in feeds, hand order after
  plays, returns and mulligans, runes/exhaust/tokens/peek/Burn Out, bad commands fail `invalid` and change nothing.
- `tests/test_faults.cpp`: random command sequences, each fault alone and all together (fixed seeds): what leaves the
  hand is what was reported, the hand is true after every recovery, everything after a final rescan, one end per seq,
  no unasked recovery without faults. `TABLE_DEBUG=1` prints failures and corrections.
- `services/game/tests/test_table_moves.cpp` (label `riftbound`): engine games replayed on the sim after every action,
  and `SoakUnderStress` (200 random-player games).

## Gotchas

- `buddy/table/debug`, not `/state`: web streams every retained `buddy/+/state` to the browser, and this one holds
  Buddy's hand.
- The table plays exactly the id asked. Of same-code copies game asks for the nearest (`game::nearest_copy`): else the
  engine's hand order stops being the pile's.
- A double feed can land a same-code copy further up at the bottom: the confirm read accepts it, the belief's order
  drifts until the next recovery. Same codes, so nothing wrong gets played; the final rescan fixes it.
- Ids are the table's, per game from 1 (legend, champion); a card going into a deck loses its id.
- `Sim::compare` needs the hand tray: `tracker().hand_pile()`.
