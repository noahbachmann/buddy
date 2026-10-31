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
inline constexpr int channel_per_turn = 2;  // 315.3
inline constexpr int domain_count = 6;      // bit order of riftbound::domain

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

// Keywords as characteristics (801–803). Assault and Shield carry a value that adds up when granted again (807.2);
// the others are present or not. Their rules come with #25/#26; here they are only granted, lost and checked.
enum class Keyword : std::uint8_t {
    Accelerate, Ambush, Assault, Deathknell, Deflect, Ganking, Hidden, Shield, Tank, Temporary, Vision,
};
inline constexpr int keyword_count = 11;
std::string_view to_string(Keyword k);

struct Keywords {
    std::array<std::uint8_t, keyword_count> value{};  // 0 = absent

    bool has(Keyword k) const { return value[static_cast<int>(k)] > 0; }
    int get(Keyword k) const { return value[static_cast<int>(k)]; }
    void grant(Keyword k, int n = 1);  // Assault/Shield add up, others stay 1
    void lose(Keyword k) { value[static_cast<int>(k)] = 0; }
    bool operator==(const Keywords&) const = default;
};

// An object's characteristics after the layers (475–482): what the rules read, never stored.
struct Traits {
    int might = 0;
    Keywords keywords;
    bool operator==(const Traits&) const = default;
};

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
    bool empowered = false;    // 442, 828
    Designation designation = Designation::None;

    ObjRef ref(ObjId id) const { return {id, gen}; }
    bool operator==(const Object&) const = default;
};

// Energy and Power waiting to be spent (165). Empties at the start of each Main Phase and the end of each turn (167).
struct Pool {
    std::int16_t energy = 0;
    std::array<std::int8_t, domain_count> power{};  // by domain index (domain_index())
    std::int8_t universal = 0;                      // pays any domain (163.2.b)
    bool operator==(const Pool&) const = default;
};

// 0 for Fury … 5 for Order; `d` has exactly one domain bit.
int domain_index(Domains d);

// Zones are id lists. Decks: back = top. Hand, trash, banishment are unordered by rule (108); their order here is
// just deterministic. Champion and legend zone hold at most one card.
struct Player {
    std::vector<ObjId> main_deck, rune_deck, hand, trash, banishment, champion, legend;
    std::vector<ObjId> base;  // permanents and runes in this player's base (107.1.c)
    Pool pool;
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

// A cost (131, 356.7): Energy, Power owed in `domains` (0 = any domain), and the non-standard costs the decks' cards
// and abilities use. "self" is the card being played or the ability's source.
struct Cost {
    int energy = 0;
    int power = 0;
    Domains domains = 0;
    std::uint8_t discard = 0;        // "discard N": cards from hand, chosen while paying
    std::uint8_t recycle_trash = 0;  // "recycle N from your trash"
    bool exhaust_self = false;       // [E]
    bool kill_self = false;          // "Kill this"
    bool disempower_self = false;    // "Disempower me"
    bool operator==(const Cost&) const = default;
};
Cost operator+(Cost a, const Cost& b);  // additional costs (356.2); Power domains: a's unless a owes none

// Where a game object is: zone, battlefield index, controller. Events note it before a card leaves (383.2.c.3.b).
struct Place {
    Zone zone = Zone::Removed;
    std::uint8_t where = 0;
    PlayerId controller = 0;
    bool operator==(const Place&) const = default;
};

// A game action or change of state that abilities react to (370.1.a). Game actions emit them (actions.hpp); rules
// read them in triggers() and replace(). #25 adds Moved, Conquer, Hold, Attack, Defend.
enum class EventKind : std::uint8_t {
    Played,      // a card was played: a permanent entered (383.4.a), a spell was finalized
    Activated,   // an activated ability resolved (377.2.a)
    Die,         // `subject` would die (429): replaceable; Deathknell triggers before it moves (383.2.c.3)
    Died,        // `subject` died: for everyone else, after the move (383.2.c)
    Damage,      // `amount` would be dealt to `subject` (418): replaceable (prevention)
    Discarded,   // 423
    Empowered,   // 827.2.a, 828.1.d
    Targeted,    // `subject` was chosen by the item of `source` (383.4.b)
    PhaseStart,  // `amount` = Phase: Beginning (315.2.a), Main (316), Ending (317.1.a: "at end of turn")
};
std::string_view to_string(EventKind kind);

struct Event {
    EventKind kind = EventKind::Played;
    ObjRef subject;        // the object the event is about (the card played, the unit dying, …)
    ObjRef source;         // what caused it: the spell or ability's source, the card that targeted
    PlayerId player = 0;   // who did it: the player playing, discarding; the controller of the dying unit
    Place place;           // where `subject` was (Die, Died)
    std::int16_t amount = 0;
    bool alone = false;    // Die/Died: no other friendly unit at its place (Loyal Poro, 808)
    bool cancelled = false;  // a replacement replaced it (370.1.a.1): it didn't happen
    bool operator==(const Event&) const = default;
};
inline Event make_event(EventKind kind, ObjRef subject, PlayerId player, ObjRef source = {}) {
    Event e;
    e.kind = kind;
    e.subject = subject;
    e.player = player;
    e.source = source;
    return e;
}

// A chosen target (355.7) or picked object (not targeted). A player target has no object: ref.id = no_obj.
struct Target {
    ObjRef ref;
    PlayerId player = 0;
    std::uint8_t slot = 0;  // index into the item's TargetSpecs
    bool operator==(const Target&) const = default;
};

enum class ItemKind : std::uint8_t { Card, Activated, Triggered };  // a reflexive trigger is Triggered too (389)
inline constexpr std::uint8_t card_effect = 0xff;  // ChainItem::ability of a card: its own effect

// A card or ability on the chain (327): pending until finalized (329: choices, cost, payment), then finalized.
// Permanents leave as soon as they're finalized (337.2, 359.2); spells and abilities wait for both players to pass
// (340), then resolve as a step machine that can stop for decisions. Abilities have no card (377.3.a.1): their
// rules come from the source object's card, whose id stays valid after it leaves (393).
struct ChainItem {
    ObjId card = no_obj;  // no_obj: an ability
    ObjRef source;        // the card itself, or the ability's source
    PlayerId controller = 0;
    bool finalized = false;
    bool resolving = false;
    ItemKind kind = ItemKind::Card;
    std::uint8_t ability = card_effect;  // index into the source's CardRules::abilities()
    std::uint8_t step = 0;   // finalize (pending) or resolve (finalized) progress
    std::uint8_t sub = 0;    // slot / optional cost being decided; free for resolution helpers
    std::uint8_t paid = 0;   // optional costs paid, bit per CardRules::optional_costs() (355.1.a)
    Cost cost;               // total cost (356), fixed once determined
    std::vector<Target> targets;
    std::array<std::int16_t, 4> vars{};  // linked instructions' memory during resolution (359.3.e.14)
    Event event;             // the trigger condition's information (359.3.f.3)

    bool operator==(const ChainItem&) const = default;
};

// A triggered ability whose condition was met, waiting to be put on the chain (383.3, 402.2).
struct Trigger {
    ObjRef source;
    std::uint8_t ability = 0;
    PlayerId controller = 0;
    Event event;
    bool operator==(const Trigger&) const = default;
};

enum class Duration : std::uint8_t {
    Turn,         // "this turn": expires in the Expiration Step (317.2.c)
    Combat,       // "this combat" (#25 ends them)
    WhileSource,  // "as long as …": while the source is the same object on the board
    Zone,         // no duration given (801.3.a.3): while the subject stays the same object
};

// A temporary modification from a resolved spell or ability, applied through the layers (479). Plain data; the
// list order is the timestamp order (482). Gone once its subject changes zones to or from a non-board zone (124).
enum class ModKind : std::uint8_t { BaseMight, Might, RaiseMight, Grant, Lose };
struct Effect {
    ObjRef subject;
    ObjRef source;
    ModKind kind = ModKind::Might;
    Keyword keyword = Keyword::Assault;
    std::int16_t value = 0;
    Duration duration = Duration::Turn;
    bool operator==(const Effect&) const = default;
};

// 390: an ability created by another, active for a window, not tied to any card on the board (393). Its rules are the
// creator's ability `ability`; a trigger fires through CardRules::triggers, a replacement through CardRules::replace.
struct Delayed {
    ObjRef source;
    std::uint8_t ability = 0;
    PlayerId controller = 0;
    ObjRef subject;  // what it watches ("the next time it would die"), if anything
    Duration window = Duration::Turn;
    bool once = true;  // "the next time": gone after it applied or triggered
    bool operator==(const Delayed&) const = default;
};

// "Once each turn", "the first time … each turn" (383.3.e, 386): how often an ability was used this turn.
struct Use {
    ObjRef source;
    std::uint8_t ability = 0;
    std::uint8_t count = 0;
    bool operator==(const Use&) const = default;
};

// Linked abilities (394–398): objects an ability of `source` affected, for the abilities linked to it.
struct Link {
    ObjRef source;
    ObjRef object;
    bool operator==(const Link&) const = default;
};

// 310: Neutral/Showdown × Open/Closed.
enum class TurnState : std::uint8_t { NeutralOpen, NeutralClosed, ShowdownOpen, ShowdownClosed };
std::string_view to_string(TurnState state);

enum class Phase : std::uint8_t { Setup, Awaken, Beginning, Channel, Draw, Main, Ending };
std::string_view to_string(Phase phase);

// Rules procedures waiting to run (333). A task that needs a choice opens a Decision and the engine stops; the
// answer continues it, so no procedure ever waits on the C++ stack.
enum class TaskKind : std::uint8_t {
    Mulligan,
    Awaken, Beginning, Channel, Draw, Main,  // start of turn 315, main phase 316
    Ending,                                  // 317.1 ending step
    Expiration,                              // 317.2: Ending Special Cleanup, then the next player's turn (317.3)
};
struct Task {
    TaskKind kind;
    PlayerId player = 0;
    bool operator==(const Task&) const = default;
};

// What the engine waits for. Exactly one player decides at a time (303.2). Picking several things is a sequence of
// Pick actions and Done; picks go in zone order, so each set has one path (no permutations for search).
enum class DecisionKind : std::uint8_t {
    None,      // nothing to decide: game over
    Mulligan,  // 117: pick up to 2 hand cards to set aside, then Done
    Priority,  // 312: Play a card, Activate an ability, then EndTurn (Neutral Open, 316.9) or Pass (Closed, 338.1.b)
    Recycle,   // paying Power (357): pick the rune to recycle when runes of several domains could (see pay.hpp)
    Choose,    // pick min..max of `options` (in their order), then Done; `purpose` says what for
};
std::string_view to_string(DecisionKind kind);

// What a Choose decision answers. Whoever asked continues with GameState::answer.
enum class Purpose : std::uint8_t {
    None,
    Target,        // 355.5–355.13: the targets of one TargetSpec slot
    May,           // 383.3.a: perform a "you may" trigger (Pick = yes, Done = no)
    OptionalCost,  // 355.1.a: pay an optional additional cost (Pick = yes)
    Discard,       // a cost: which cards to discard
    RecycleTrash,  // a cost: which trash cards to recycle
    TriggerOrder,  // 383.3.d: which of your triggers goes on the chain next
    Effect,        // a choice while resolving (355.18)
};
std::string_view to_string(Purpose purpose);

enum class ActionKind : std::uint8_t { Pick, Done, Play, EndTurn, Pass, Activate };
// `n`: Activate = ability index; Pick of a player = the player (obj = no_obj); free for other options.
struct Action {
    ActionKind kind = ActionKind::Done;
    ObjId obj = no_obj;
    std::uint8_t n = 0;
    bool operator==(const Action&) const = default;
};

struct Decision {
    DecisionKind kind = DecisionKind::None;
    PlayerId player = 0;
    std::vector<ObjId> picked;  // Mulligan, Recycle
    // Choose: options in a fixed order; picks go in that order, so each set has one path.
    Purpose purpose = Purpose::None;
    std::vector<Action> options;
    std::vector<std::uint8_t> chosen;  // indexes into options, ascending
    std::uint8_t min = 0, max = 0;
    bool operator==(const Decision&) const = default;
};
inline Decision decide(DecisionKind kind, PlayerId player) {
    Decision d;
    d.kind = kind;
    d.player = player;
    return d;
}

struct GameState {
    std::array<Player, player_count> players;
    std::vector<Object> objects;
    std::vector<Battlefield> battlefields;
    std::vector<ChainItem> chain;  // back = newest
    // Priority passes in a row on the chain (339): reset when an item is added or leaves. The holder is the newest
    // item's controller (337.4, 340.4) plus this many steps in turn order, so it needs no field of its own.
    std::uint8_t passes = 0;
    bool showdown = false;  // 308.1: showdown or combat in progress. Only timing reads it until #25 runs them
    int turn = 0;  // 1 = the first player's first turn
    PlayerId first_player = 0;
    PlayerId turn_player = 0;
    Phase phase = Phase::Setup;
    std::optional<PlayerId> winner;
    bool cleanup_due = false;  // 319: the engine runs a Cleanup before its next task
    std::vector<Task> tasks;  // stack: back runs next
    Decision decision;
    std::optional<std::vector<Action>> answer;  // a finished Choose decision's picks, until its asker takes them
    Rng rng;
    bool vanilla = false;  // GameOptions::vanilla: no card rules run, stats and timing only

    // Abilities: plain data, see the types above.
    std::vector<Trigger> triggered;  // conditions met, not yet on the chain
    std::vector<Effect> effects;     // timestamp order
    std::vector<Delayed> delayed;
    std::vector<Use> uses;           // this turn
    std::vector<Link> links;
    std::uint8_t deaths_this_turn = 0;  // "if a unit died this turn" (capped at 255)

    Object& obj(ObjId id) { return objects.at(id); }
    const Object& obj(ObjId id) const { return objects.at(id); }
    Player& player(PlayerId p) { return players.at(p); }
    const Player& player(PlayerId p) const { return players.at(p); }
    bool alive(ObjRef r) const { return r.id < objects.size() && objects[r.id].gen == r.gen; }

    bool operator==(const GameState&) const = default;
};

TurnState turn_state(const GameState& s);
inline bool closed(const GameState& s) { return !s.chain.empty(); }  // 309.1
// Who holds priority while the chain is finalized and nothing is pending (312.2.c–d).
PlayerId chain_priority(const GameState& s);
// Might after the layers (actions.hpp → traits()).
int might(const GameState& s, ObjId id);
// 142.4.b: a non-zero amount ≥ Might.
bool lethal_damage(const GameState& s, ObjId id);

// The id list a card in `zone` sits in. Non-board zones are the owner's (056), Base the controller's,
// Battlefield/Facedown the battlefield's `where`. Chain and Removed have none.
std::vector<ObjId>* zone_list(GameState& s, Zone zone, PlayerId player, std::uint8_t where = 0);

enum class Pos { Top, Bottom };
// Moves a card. Into or out of a non-board zone it becomes a new object (124): gen + 1, damage, buffs, statuses and
// designation cleared, controller back to the owner. No events, no replacements: game actions (actions.hpp) do that.
void move_to(GameState& s, ObjId id, Zone zone, PlayerId controller, std::uint8_t where = 0, Pos pos = Pos::Top);
// Top of the main deck into the hand. An empty deck burns out (432) first: as often as it takes, so with an empty
// trash too the opponent ends up winning (432.3.c).
void draw(GameState& s, PlayerId p, int n);
// 432.2: trash into the main deck, the opponent gains 1. `in_sequence` counts this action's burn outs: from the 2nd
// on, reaching the Victory Score wins at once (432.3.c); the first waits for a cleanup (323.1).
void burn_out(GameState& s, PlayerId p, int in_sequence);
// 431: top of the rune deck into the base, ready; fewer if the deck runs out.
void channel(GameState& s, PlayerId p, int n);
// 417: to the bottom of the owner's main or rune deck; two or more in random order (417.5).
void recycle(GameState& s, std::vector<ObjId> ids);

// Every object in exactly one zone list (or chain item), matching its zone/owner/controller/where; occupancy of
// single-card zones; pools ≥ 0; a sane decision. Empty = fine. The random games check it after every action.
std::vector<std::string> check_invariants(const GameState& s);

} // namespace buddy::riftbound
