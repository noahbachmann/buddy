#pragma once
// Game actions that abilities see (413–429): each one runs replacement effects first (367), performs what's left
// and emits events that queue triggered abilities (382). Plus the layers (475–482) that turn printed stats, buffs,
// passives and continuous effects into Traits. move_to (state.hpp) stays the raw zone change underneath.

#include <optional>
#include <vector>

#include "state.hpp"

namespace buddy::riftbound {

// Who performs an action and with what (429.5): the spell or ability's source, its controller.
struct Cause {
    ObjRef source;
    PlayerId player = 0;
};

// ---- Layers (475–482) ----
// Printed might and keywords, then until nothing new applies (478): layer 2 base might, layer 3 keyword grants and
// losses (timestamp order: passives by source, then continuous effects), layer 4 increases (buffs, +N), "increased
// to N" (depends on the increases, 481), decreases. Objects a passive reads are evaluated first (478.1.a); a loop
// deeper than a few objects falls back to printed values ⚠️ (478.1.c: no deck has one).
Traits traits(const GameState& s, ObjId id);
bool has(const GameState& s, ObjId id, Keyword k);
bool mighty(const GameState& s, ObjId id);  // 708

Place place_of(const GameState& s, ObjId id);
// Controller on the board, owner elsewhere: who controls the object's abilities (374, 378).
PlayerId controller_of(const GameState& s, ObjId id);

// ---- Events ----
// Queues the triggered abilities `e` meets (GameState::triggered): active cards in turn-independent id order and
// delayed triggers. A Die event reaches only its subject (its own leave-the-board triggers, 383.2.c.3); everything
// else hears Died afterwards. Nothing in vanilla mode.
void emit(GameState& s, const Event& e);
// Applies replacement effects to `e` (delayed ones first, newest first, then static ones in id order ⚠️: 372's
// choice of order is not a decision yet; no deck has two for one event). Returns true if `e` no longer happens.
bool replaced(GameState& s, Event& e);

// ---- Game actions ----
// 429: kills `id` (a permanent on the board). Returns false if a replacement saved it (negated, 359.3.e.14.b).
bool kill(GameState& s, ObjId id, const Cause& cause);
// Simultaneous kills (cleanup, "kill all"): replacements per event, then every Die (all still on the board, so
// "alone" counts the others dying too), then the moves, then Died. Returns the ones that died.
std::vector<ObjId> kill_all(GameState& s, const std::vector<ObjId>& ids, const Cause& cause);
// 418: marks damage after prevention; lethal damage kills in the next cleanup. Returns the damage dealt.
int deal(GameState& s, ObjId id, int amount, const Cause& cause);
void heal(GameState& s, ObjId id);     // 419: all damage
void exhaust(GameState& s, ObjId id);  // 415
void ready(GameState& s, ObjId id);    // 416
void recall(GameState& s, ObjId id);   // 457: to its controller's base, not a move
void stun(GameState& s, ObjId id);     // 424
bool buff(GameState& s, ObjId id);     // 427: +1 buff if it has none; false if it had one
void discard(GameState& s, ObjId id, const Cause& cause);  // 423: hand → trash
void empower(GameState& s, ObjId id);  // 442: Empowered event if it wasn't
void disempower(GameState& s, ObjId id);

// ---- Continuous effects (479) and durations ----
void give(GameState& s, const Effect& e);
// "+N / −N [to a minimum of M] this turn" from a spell or ability: the limit is snapshotted now (479.4.b), an
// increase by a negative amount is 0 (479.4.c).
void give_might(GameState& s, ObjId subject, int delta, ObjRef source, Duration duration,
                std::optional<int> minimum = std::nullopt);
// "Double its Might": +its current Might, at least 0 (479.4.c).
void double_might(GameState& s, ObjId subject, ObjRef source, Duration duration);
// Ends every effect and delayed window of `d` (Turn: 317.2.c; Combat: #25).
void expire(GameState& s, Duration d);
// Drops effects whose subject changed objects or whose WhileSource source left the board, and delayed abilities
// watching a subject that's gone. Run by every cleanup.
void prune(GameState& s);

// ---- Bookkeeping for abilities ----
int uses(const GameState& s, ObjRef source, std::uint8_t ability);  // this turn
void use(GameState& s, ObjRef source, std::uint8_t ability, int delta = 1);
void link(GameState& s, ObjRef source, ObjRef object);               // 394
std::vector<ObjId> linked(const GameState& s, ObjRef source);       // those still the same object

} // namespace buddy::riftbound
