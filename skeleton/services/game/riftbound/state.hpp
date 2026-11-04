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
inline constexpr std::uint8_t to_base = 0xff;  // a location's battlefield index meaning "the base" (moves, 355.2)

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
inline bool on_board(Zone zone) { return zone == Zone::Base || zone == Zone::Battlefield || zone == Zone::Facedown; }
// Where card rules work (365, 384): the board and the legend zones. GameState::active lists the objects there.
inline bool active_zone(Zone zone) { return on_board(zone) || zone == Zone::LegendZone; }
// Everyone may look at the cards in it (128.5): not the decks (108.4.e, 108.5.d), hands (108.7.c), facedown cards
// (107.3.f) or cards set aside.
bool is_public(Zone zone);
inline constexpr std::uint8_t everyone = (1u << player_count) - 1;  // Object::known

enum class Designation : std::uint8_t { None, Attacker, Defender };  // during combat (461)

// Keywords as characteristics (801–803). Assault, Shield and Deflect carry a value that adds up when granted again
// (807.2, 809.2); the others are present or not. Hidden and Temporary have no behaviour yet (no deck card has them).
// Keyword abilities that are no characteristic the decks check (Empower, Equip, Flow, Repeat) live in CardRules.
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
    ObjId attached = no_obj;   // the top-most card this gear is attached to (716–719); same place as it
    // Bit per player who knows which card this is (128): everyone in a public zone; the owner in their hand, the
    // controller facedown, the predicting player (817); kept into a deck until it's shuffled. view() hides the rest.
    std::uint8_t known = 0;

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
    std::optional<PlayerId> contested;   // who applied Contested (190.3.a); staging follows from it (323.7–323.9)
    std::vector<ObjId> here;             // units and gear at this battlefield, every player's
    std::vector<ObjId> facedown;         // 107.3: at most one card

    bool operator==(const Battlefield&) const = default;
};

// A cost (131, 356.7): Energy, Power owed in `domains` (0 = any domain) plus Power of any domain ([A]: Deflect 809,
// Flow and Equip costs), and the non-standard costs the decks' cards and abilities use. "self" is the card being
// played or the ability's source.
struct Cost {
    int energy = 0;
    int power = 0;
    Domains domains = 0;
    std::uint8_t discard = 0;        // "discard N": cards from hand, chosen while paying
    std::uint8_t recycle_trash = 0;  // "recycle N from your trash"
    bool exhaust_self = false;       // [E]
    bool kill_self = false;          // "Kill this"
    bool disempower_self = false;    // "Disempower me"
    int any_power = 0;               // [A]
    std::uint8_t burn = 0;           // "[Burn N]" (441.3.a)
    bool discard_spell = false;      // the discarded cards must be spells ("Discard a spell")
    bool banish_self = false;        // "banish me"
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
// read them in triggers() and replace().
enum class EventKind : std::uint8_t {
    Played,      // a card was played: a permanent entered (383.4.a; `amount` = ChainItem::paid), a spell was finalized
    Activated,   // an activated ability resolved (377.2.a)
    Die,         // `subject` would die (429): replaceable; Deathknell triggers before it moves (383.2.c.3)
    Died,        // `subject` died: for everyone else, after the move (383.2.c)
    Damage,      // `amount` would be dealt to `subject` (418): replaceable (prevention)
    Discarded,   // 423
    Empowered,   // 827.2.a, 828.1.d
    Targeted,    // `subject` was chosen by the item of `source` (383.4.b)
    PhaseStart,  // `amount` = Phase: Beginning (315.2.a), Main (316), Ending (317.1.a: "at end of turn")
    Moved,       // `subject` moved (421, 447); `place` = its origin. Not on recalls (458.1)
    Conquer,     // `player` conquered battlefield `subject` (471.1); `amount` = excess combat damage they assigned
    Hold,        // `player` held battlefield `subject` (471.2)
    CombatStart, // combat starts at battlefield `subject` (466.2.b); `player` = the attacker
    Attack,      // `subject` became an attacker as combat started (466.2.c.3)
    Defend,      // `subject` became a defender as combat started
    Banished,    // `player` banished `subject` (428); `place` = its origin
};
std::string_view to_string(EventKind kind);

struct Event {
    EventKind kind = EventKind::Played;
    ObjRef subject;        // the object the event is about (the card played, the unit dying, …)
    ObjRef source;         // what caused it: the spell or ability's source, the card that targeted
    PlayerId player = 0;   // who did it: the player playing, discarding; the controller of the dying unit
    Place place;           // where `subject` was (Die, Died, Moved: origin) or entered (Played)
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
// Keyword abilities the engine runs itself, past every card's own ability indexes (rules.hpp ability_of).
inline constexpr std::uint8_t ability_vision = 0xf0;  // 817: "When this is played, predict."

// How a card was played (ChainItem::play), bits.
namespace played {
inline constexpr std::uint8_t flow = 1;       // from the trash for its Flow cost: banished, not trashed (829)
inline constexpr std::uint8_t ambush = 2;     // only Ambush made it playable now: its battlefields only (822)
inline constexpr std::uint8_t to_base = 4;    // an effect played it "to your base"
inline constexpr std::uint8_t exhausted = 8;  // an effect played it "exhausted" (184.1)
}

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
    std::uint8_t to = to_base;  // a unit's location (355.2): battlefield index or to_base
    std::uint8_t play = 0;   // played:: bits
    std::uint8_t rep = 0;    // which execution is resolving: 0, then one more per paid Repeat (820)
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
    Combat,       // "this combat": expires as combat ends (468.7.c)
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
    Awaken, Beginning, Hold, Channel, Draw, Main,  // start of turn 315 (Hold = 315.2.b scoring step), main phase 316
    Ending,                                        // 317.1 ending step
    Expiration,     // 317.2: Ending Special Cleanup, then the next player's turn (317.3)
    StandardMove,   // 144: `player` moves units to `where` (a battlefield or to_base); asks which
    Stage,          // 323.11–323.12: the turn player picks where a staged showdown or combat begins
    ShowdownEnd,    // 348.2: a non-combat showdown closed
    CombatDamage,   // 467 + 468.1: assign and deal combat damage, Combat Special Cleanup
    CombatResult,   // 468.3–468.5: result, establish control
    CombatEnd,      // 468.7
};
struct Task {
    TaskKind kind;
    PlayerId player = 0;
    std::uint8_t where = 0;  // StandardMove: destination
    bool operator==(const Task&) const = default;
};

// Combat damage one player assigned to one unit (467.2.c), dealt once both have assigned.
struct Assigned {
    ObjId unit = no_obj;
    std::int16_t amount = 0;
    bool operator==(const Assigned&) const = default;
};

// The showdown or combat in progress (341–348, 461–468); a combat is a showdown with `combat` set. While `window` is
// open the player with focus acts (Play, Activate, Pass); once all passed, the closing steps run as tasks.
struct Showdown {
    std::uint8_t where = 0;     // index into GameState::battlefields
    bool combat = false;
    bool window = true;
    PlayerId focus = 0;         // 345, 466.2.c.1.a
    std::uint8_t passes = 0;    // focus passes in a row (347.2.a)
    bool pass_focus = false;    // the chain was opened by a play or activation: focus passes when it closes (346.1)
    PlayerId attacker = 0;      // 466.2.c.1
    std::int16_t left = -1;     // damage the current assigner still has to assign; -1 = not started
    std::uint8_t assigners = 0;  // players done assigning, attacker first
    std::array<std::int16_t, player_count> excess{};  // assigned beyond lethal (Vi's legend, Yeti Brawler)
    std::vector<Assigned> assigned;
    bool operator==(const Showdown&) const = default;
};

// What the engine waits for. Exactly one player decides at a time (303.2). Picking several things is a sequence of
// Pick actions and Done; picks go in zone order, so each set has one path (no permutations for search).
enum class DecisionKind : std::uint8_t {
    None,      // nothing to decide: game over
    Mulligan,  // 117: pick up to 2 hand cards to set aside, then Done
    Priority,  // 312: Play, Activate, Move (Neutral Open: Standard Move, 144), then EndTurn (Neutral Open, 316.9) or
               // Pass (Closed: 338.1.b; Showdown Open: pass focus, 347.2)
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
    Location,      // 355.2: where a unit enters (Pick obj = battlefield card, n = index; base: no_obj, to_base)
    Move,          // 144.3: which units make the Standard Move (one or more)
    Stage,         // 323.11–323.12: the battlefield where a showdown or combat begins
    Assign,        // 467.2.c: the unit that gets combat damage next
};
std::string_view to_string(Purpose purpose);

enum class ActionKind : std::uint8_t { Pick, Done, Play, EndTurn, Pass, Activate, Move };
// `n`: Activate = ability index; Pick of a player = the player (obj = no_obj); Move = destination battlefield index
// (obj = its card) or to_base (obj = no_obj); free for other options.
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
    // Ids of the objects in an active_zone(), ascending (battlefield cards too): what the rules' object loops walk
    // instead of every object (most sit in decks and hands). Kept by move_to and create.
    std::vector<ObjId> active;
    std::vector<Battlefield> battlefields;
    std::vector<ChainItem> chain;  // back = newest
    // Priority passes in a row on the chain (339): reset when an item is added or leaves. The holder is the newest
    // item's controller (337.4, 340.4) plus this many steps in turn order, so it needs no field of its own.
    std::uint8_t passes = 0;
    std::optional<Showdown> showdown;  // 308.1, 343: a showdown or combat in progress
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

// Physical moves for the table (5.5, game_core's table_moves): while this thread has a log installed, move_to and
// create append to it, in the order the cards move. Only the referee installs one, around new_game and apply; search
// copies never log. Off, it costs one thread-local load per move.
struct LoggedMove {
    ObjId id = no_obj;
    Zone from = Zone::Removed, to = Zone::Removed;
    std::uint8_t from_where = 0, to_where = 0;
    Pos pos = Pos::Top;
    bool created = false;
    bool operator==(const LoggedMove&) const = default;
};
inline thread_local std::vector<LoggedMove>* move_log = nullptr;
// Moves a card. Into or out of a non-board zone it becomes a new object (124): gen + 1, damage, buffs, statuses and
// designation cleared, controller back to the owner. No events, no replacements: game actions (actions.hpp) do that.
// Attached gear goes along on the board (719.3.a) and detaches where it is when its top-most card leaves the board
// (719.5, 436.4.b). A token put into a non-board zone other than the chain stops existing: Removed (186.1).
void move_to(GameState& s, ObjId id, Zone zone, PlayerId controller, std::uint8_t where = 0, Pos pos = Pos::Top);
// A new object (a token, 440) in `zone`, owned and controlled by `owner`.
ObjId create(GameState& s, const Card* card, PlayerId owner, Zone zone, std::uint8_t where = 0);
// Top of the main deck into the hand. An empty deck burns out (432) first: as often as it takes, so with an empty
// trash too the opponent ends up winning (432.3.c).
void draw(GameState& s, PlayerId p, int n);
// 432.2: trash into the main deck, the opponent gains 1. `in_sequence` counts this action's burn outs: from the 2nd
// on, reaching the Victory Score wins at once (432.3.c); the first waits for a cleanup (323.1).
void burn_out(GameState& s, PlayerId p, int in_sequence);
// 441: top of the main deck into the trash; an empty deck burns out and the rest follows (441.4).
void burn(GameState& s, PlayerId p, int n);
// 431: top of the rune deck into the base, ready; fewer if the deck runs out.
void channel(GameState& s, PlayerId p, int n);
// 417: to the bottom of the owner's main or rune deck; two or more in random order (417.5), so nobody knows which
// is which any more (Object::known cleared).
void recycle(GameState& s, std::vector<ObjId> ids);

// Every object in exactly one zone list (or chain item), matching its zone/owner/controller/where; occupancy of
// single-card zones; pools ≥ 0; a sane decision. Empty = fine. The random games check it after every action.
std::vector<std::string> check_invariants(const GameState& s);

} // namespace buddy::riftbound
