#pragma once
// The state of one Riftbound Duel (Core Rules 2026-10-09, rule 487) as a plain value: copying a GameState copies the
// whole game, so search (5.4) can clone it freely. Objects and zones refer to each other by index; the only pointers
// go to immutable card data (CardDb). Design: docs/plans/05-riftbound.md → "5.2 design".

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "card_db.hpp"
#include "rng.hpp"

namespace buddy::riftbound {

using PlayerId = std::uint8_t;
inline constexpr int player_count = 2;   // Duel only (487.1)
inline constexpr int victory_score = 8;  // 487.3
inline constexpr int opening_hand = 4;   // 116
inline constexpr int max_mulligan = 2;   // 117.1

inline PlayerId opponent(PlayerId p) { return static_cast<PlayerId>(1 - p); }

// Index into GameState::objects. Stays with the physical card for the whole game (the table service tracks cards by
// it); gen tells the rules objects apart that the card becomes on each zone change (124).
using ObjId = std::uint16_t;
inline constexpr ObjId no_obj = 0xffff;

// One rules object: a reference taken by a target or an effect stops matching once the card changes zones (124).
struct ObjRef {
    ObjId id = no_obj;
    std::uint16_t gen = 0;
    bool operator==(const ObjRef&) const = default;
};

enum class Zone : std::uint8_t {
    MainDeck, RuneDeck, Hand, Trash, Banishment, ChampionZone, LegendZone,  // a player's non-board zones (108)
    Base, Battlefield, Facedown,                                             // the board (107)
    Chain,                                                                   // 108.1
    Removed,  // in no zone: 487.5's unused battlefields; cards set aside mid-procedure (117.1)
};
std::string_view to_string(Zone zone);
bool on_board(Zone zone);

enum class Designation : std::uint8_t { None, Attacker, Defender };  // during combat (461)

struct Object {
    const Card* card = nullptr;
    PlayerId owner = 0;
    PlayerId controller = 0;
    Zone zone = Zone::Removed;
    std::uint8_t where = 0;    // Battlefield, Facedown: index into GameState::battlefields
    std::uint16_t gen = 0;     // +1 on every move to or from a non-board zone (124)
    std::int16_t damage = 0;   // 142
    std::int8_t buffs = 0;     // 701
    bool exhausted = false;
    bool stunned = false;
    bool facedown = false;
    Designation designation = Designation::None;

    ObjRef ref(ObjId id) const { return {id, gen}; }
    bool operator==(const Object&) const = default;
};

// Zones are id lists. Decks: back = top. Hand, trash, banishment are unordered by rule (108); their order here is
// just deterministic. Champion and legend zone hold at most one card.
struct Player {
    std::vector<ObjId> main_deck, rune_deck, hand, trash, banishment, champion, legend;
    std::vector<ObjId> base;  // permanents and runes in this player's base (107.1.c)
    int points = 0;
    std::uint8_t scored = 0;  // battlefields scored this turn, bit per GameState::battlefields index (469)

    bool operator==(const Player&) const = default;
};

struct Battlefield {
    ObjId card = no_obj;
    std::optional<PlayerId> controller;  // 190
    std::vector<ObjId> here;             // units and gear at this battlefield, every player's
    std::vector<ObjId> facedown;         // 107.3: at most one card

    bool operator==(const Battlefield&) const = default;
};

// A card or ability being played (327). #23 adds targets, costs and the resolution step.
struct ChainItem {
    ObjId card = no_obj;  // no_obj: an ability
    ObjRef source;
    PlayerId controller = 0;
    bool finalized = false;
    bool operator==(const ChainItem&) const = default;
};

enum class Phase : std::uint8_t { Setup, Awaken, Beginning, Channel, Draw, Main, Ending };
std::string_view to_string(Phase phase);

// Rules procedures waiting to run (333). A task that needs a choice opens a Decision and the engine stops; the
// answer continues it, so no procedure ever waits on the C++ stack.
enum class TaskKind : std::uint8_t { Mulligan };
struct Task {
    TaskKind kind;
    PlayerId player = 0;
    bool operator==(const Task&) const = default;
};

// What the engine waits for. Exactly one player decides at a time (303.2). Picking several things is a sequence of
// Pick actions and Done; picks go in zone order, so each set has one path (no permutations for search).
enum class DecisionKind : std::uint8_t {
    None,      // nothing to decide: game over, or the engine ran out of rules (until #22: after setup)
    Mulligan,  // 117: pick up to 2 hand cards to set aside, then Done
};
std::string_view to_string(DecisionKind kind);

struct Decision {
    DecisionKind kind = DecisionKind::None;
    PlayerId player = 0;
    std::vector<ObjId> picked;
    bool operator==(const Decision&) const = default;
};

enum class ActionKind : std::uint8_t { Pick, Done };
struct Action {
    ActionKind kind = ActionKind::Done;
    ObjId obj = no_obj;
    bool operator==(const Action&) const = default;
};

struct GameState {
    std::array<Player, player_count> players;
    std::vector<Object> objects;
    std::vector<Battlefield> battlefields;
    std::vector<ChainItem> chain;
    int turn = 0;  // 1 = the first player's first turn
    PlayerId first_player = 0;
    PlayerId turn_player = 0;
    Phase phase = Phase::Setup;
    std::optional<PlayerId> winner;
    std::vector<Task> tasks;  // stack: back runs next
    Decision decision;
    Rng rng;

    Object& obj(ObjId id) { return objects.at(id); }
    const Object& obj(ObjId id) const { return objects.at(id); }
    Player& player(PlayerId p) { return players.at(p); }
    const Player& player(PlayerId p) const { return players.at(p); }
    bool alive(ObjRef r) const { return r.id < objects.size() && objects[r.id].gen == r.gen; }

    bool operator==(const GameState&) const = default;
};

// The id list a card in `zone` sits in. Non-board zones are the owner's (056), Base the controller's,
// Battlefield/Facedown the battlefield's `where`. Chain and Removed have none.
std::vector<ObjId>* zone_list(GameState& s, Zone zone, PlayerId player, std::uint8_t where = 0);

enum class Pos { Top, Bottom };
// Moves a card. Into or out of a non-board zone it becomes a new object (124): gen + 1, damage, buffs, statuses and
// designation cleared, controller back to the owner.
void move_to(GameState& s, ObjId id, Zone zone, PlayerId controller, std::uint8_t where = 0, Pos pos = Pos::Top);
// Top of the main deck into the hand. Burn Out (432) when the deck is empty comes with #22; until then it throws.
void draw(GameState& s, PlayerId p, int n);
// 417: to the bottom of the owner's main or rune deck; two or more in random order (417.5).
void recycle(GameState& s, std::vector<ObjId> ids);

// Every object in exactly one zone list, matching its zone/owner/controller/where; occupancy of single-card zones;
// a sane decision. Empty = fine. The random games (#22) check it after every action.
std::vector<std::string> check_invariants(const GameState& s);

} // namespace buddy::riftbound
