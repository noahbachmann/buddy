#include "state.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>

namespace buddy::riftbound {

std::string_view to_string(Zone zone) {
    switch (zone) {
        case Zone::MainDeck: return "main deck";
        case Zone::RuneDeck: return "rune deck";
        case Zone::Hand: return "hand";
        case Zone::Trash: return "trash";
        case Zone::Banishment: return "banishment";
        case Zone::ChampionZone: return "champion zone";
        case Zone::LegendZone: return "legend zone";
        case Zone::Base: return "base";
        case Zone::Battlefield: return "battlefield";
        case Zone::Facedown: return "facedown zone";
        case Zone::Chain: return "chain";
        case Zone::Removed: return "removed";
    }
    return "?";
}

bool is_public(Zone zone) {
    switch (zone) {
        case Zone::MainDeck: case Zone::RuneDeck: case Zone::Hand: case Zone::Facedown: case Zone::Removed: return false;
        default: return true;
    }
}

std::string_view to_string(Phase phase) {
    switch (phase) {
        case Phase::Setup: return "setup";
        case Phase::Awaken: return "awaken";
        case Phase::Beginning: return "beginning";
        case Phase::Channel: return "channel";
        case Phase::Draw: return "draw";
        case Phase::Main: return "main";
        case Phase::Ending: return "ending";
    }
    return "?";
}

std::string_view to_string(TurnState state) {
    switch (state) {
        case TurnState::NeutralOpen: return "neutral open";
        case TurnState::NeutralClosed: return "neutral closed";
        case TurnState::ShowdownOpen: return "showdown open";
        case TurnState::ShowdownClosed: return "showdown closed";
    }
    return "?";
}

TurnState turn_state(const GameState& s) {
    if (s.showdown) return closed(s) ? TurnState::ShowdownClosed : TurnState::ShowdownOpen;
    return closed(s) ? TurnState::NeutralClosed : TurnState::NeutralOpen;
}

PlayerId chain_priority(const GameState& s) {
    if (s.chain.empty()) throw std::logic_error("chain_priority: no chain");
    return static_cast<PlayerId>((s.chain.back().controller + s.passes) % player_count);
}

bool lethal_damage(const GameState& s, ObjId id) {
    const Object& o = s.obj(id);
    return o.damage > 0 && o.damage >= might(s, id);
}

std::string_view to_string(DecisionKind kind) {
    switch (kind) {
        case DecisionKind::None: return "none";
        case DecisionKind::Mulligan: return "mulligan";
        case DecisionKind::Priority: return "priority";
        case DecisionKind::Recycle: return "recycle";
        case DecisionKind::Choose: return "choose";
    }
    return "?";
}

std::string_view to_string(Purpose purpose) {
    switch (purpose) {
        case Purpose::None: return "none";
        case Purpose::Target: return "target";
        case Purpose::May: return "may";
        case Purpose::OptionalCost: return "optional cost";
        case Purpose::Discard: return "discard";
        case Purpose::RecycleTrash: return "recycle from trash";
        case Purpose::TriggerOrder: return "trigger order";
        case Purpose::Effect: return "effect";
        case Purpose::Location: return "location";
        case Purpose::Move: return "move";
        case Purpose::Stage: return "stage";
        case Purpose::Assign: return "assign damage";
    }
    return "?";
}

std::string_view to_string(Keyword k) {
    switch (k) {
        case Keyword::Accelerate: return "Accelerate";
        case Keyword::Ambush: return "Ambush";
        case Keyword::Assault: return "Assault";
        case Keyword::Deathknell: return "Deathknell";
        case Keyword::Deflect: return "Deflect";
        case Keyword::Ganking: return "Ganking";
        case Keyword::Hidden: return "Hidden";
        case Keyword::Shield: return "Shield";
        case Keyword::Tank: return "Tank";
        case Keyword::Temporary: return "Temporary";
        case Keyword::Vision: return "Vision";
    }
    return "?";
}

void Keywords::grant(Keyword k, int n) {
    auto& v = value[static_cast<int>(k)];
    const bool sums = k == Keyword::Assault || k == Keyword::Shield || k == Keyword::Deflect;  // 807.2, 809.2, 814
    v = static_cast<std::uint8_t>(std::clamp(sums ? v + n : std::max<int>(v, 1), 0, 255));
}

std::string_view to_string(EventKind kind) {
    switch (kind) {
        case EventKind::Played: return "played";
        case EventKind::Activated: return "activated";
        case EventKind::Die: return "die";
        case EventKind::Died: return "died";
        case EventKind::Damage: return "damage";
        case EventKind::Discarded: return "discarded";
        case EventKind::Empowered: return "empowered";
        case EventKind::Targeted: return "targeted";
        case EventKind::PhaseStart: return "phase start";
        case EventKind::Moved: return "moved";
        case EventKind::Conquer: return "conquer";
        case EventKind::Hold: return "hold";
        case EventKind::CombatStart: return "combat start";
        case EventKind::Attack: return "attack";
        case EventKind::Defend: return "defend";
        case EventKind::Banished: return "banished";
    }
    return "?";
}

Cost operator+(Cost a, const Cost& b) {
    if (a.power == 0 && b.power > 0) a.domains = b.domains;
    a.energy += b.energy;
    a.power += b.power;
    a.discard = static_cast<std::uint8_t>(a.discard + b.discard);
    a.recycle_trash = static_cast<std::uint8_t>(a.recycle_trash + b.recycle_trash);
    a.exhaust_self = a.exhaust_self || b.exhaust_self;
    a.kill_self = a.kill_self || b.kill_self;
    a.disempower_self = a.disempower_self || b.disempower_self;
    a.any_power += b.any_power;
    a.burn = static_cast<std::uint8_t>(a.burn + b.burn);
    a.discard_spell = a.discard_spell || b.discard_spell;
    a.banish_self = a.banish_self || b.banish_self;
    return a;
}

int domain_index(Domains d) {
    if (std::popcount(d) != 1) throw std::logic_error("domain_index: not one domain");
    return std::countr_zero(d);
}

std::vector<ObjId>* zone_list(GameState& s, Zone zone, PlayerId player, std::uint8_t where) {
    Player& p = s.player(player);
    switch (zone) {
        case Zone::MainDeck: return &p.main_deck;
        case Zone::RuneDeck: return &p.rune_deck;
        case Zone::Hand: return &p.hand;
        case Zone::Trash: return &p.trash;
        case Zone::Banishment: return &p.banishment;
        case Zone::ChampionZone: return &p.champion;
        case Zone::LegendZone: return &p.legend;
        case Zone::Base: return &p.base;
        case Zone::Battlefield: return &s.battlefields.at(where).here;
        case Zone::Facedown: return &s.battlefields.at(where).facedown;
        case Zone::Chain:
        case Zone::Removed: return nullptr;
    }
    return nullptr;
}

namespace {

PlayerId list_player(const Object& o) { return on_board(o.zone) ? o.controller : o.owner; }

} // namespace

void move_to(GameState& s, ObjId id, Zone zone, PlayerId controller, std::uint8_t where, Pos pos) {
    Object& o = s.obj(id);
    const bool was_on_board = on_board(o.zone);
    if (auto* from = zone_list(s, o.zone, list_player(o), o.where)) {
        const auto it = std::find(from->begin(), from->end(), id);
        if (it == from->end()) throw std::logic_error("move_to: object not in its zone list");
        from->erase(it);
    }
    if (o.card->token && !on_board(zone) && zone != Zone::Chain) zone = Zone::Removed;  // 186.1
    if (was_on_board != on_board(zone)) s.cleanup_due = true;  // 319.6
    if (!was_on_board || !on_board(zone)) {
        ++o.gen;
        o.damage = 0;
        o.buffs = 0;
        o.exhausted = o.stunned = o.facedown = o.empowered = false;
        o.designation = Designation::None;
        o.attached = no_obj;
        controller = on_board(zone) ? controller : o.owner;
    }
    if (active_zone(o.zone) != active_zone(zone)) {
        const auto it = std::lower_bound(s.active.begin(), s.active.end(), id);
        if (active_zone(zone)) s.active.insert(it, id);
        else s.active.erase(it);
    }
    o.zone = zone;
    o.controller = controller;
    o.where = where;
    if (is_public(zone)) o.known = everyone;
    else if (zone == Zone::Hand) o.known |= 1u << o.owner;
    else if (zone == Zone::Facedown) o.known |= 1u << controller;
    if (auto* to = zone_list(s, zone, list_player(o), where)) {
        if (pos == Pos::Top) to->push_back(id);
        else to->insert(to->begin(), id);
    }
    if (!was_on_board) return;
    for (ObjId g = 0; g < s.objects.size(); ++g) {  // 719.3.a, 719.5
        Object& gear = s.obj(g);
        if (gear.attached != id) continue;
        if (!on_board(zone)) {
            gear.attached = no_obj;  // stays where it was (436.4.b); a cleanup recalls it from a battlefield
            s.cleanup_due = true;
        } else if (gear.zone != zone || gear.where != where) {
            move_to(s, g, zone, gear.controller, where);
            gear.attached = id;  // a board-to-board move keeps the object
        }
    }
}

ObjId create(GameState& s, const Card* card, PlayerId owner, Zone zone, std::uint8_t where) {
    const auto id = static_cast<ObjId>(s.objects.size());
    if (id == no_obj) throw std::logic_error("too many objects");
    Object& o = s.objects.emplace_back();
    o.card = card;
    o.owner = o.controller = owner;
    o.zone = zone;
    o.where = where;
    o.known = is_public(zone) || card->type == CardType::Battlefield ? everyone : 0;  // 487.5's unused ones: no secret
    if (active_zone(zone)) s.active.push_back(id);  // the highest id yet
    if (card->type == CardType::Battlefield) return id;  // its place is Battlefield::card (or Removed), no list
    if (auto* list = zone_list(s, zone, owner, where)) list->push_back(id);
    return id;
}

void draw(GameState& s, PlayerId p, int n) {
    auto& deck = s.player(p).main_deck;
    int burns = 0;
    for (int i = 0; i < n; ++i) {
        while (deck.empty()) {  // 432.3: an empty trash leaves the deck empty, the next try burns out again
            if (s.winner) return;
            burn_out(s, p, ++burns);
        }
        move_to(s, deck.back(), Zone::Hand, p);
    }
}

void burn(GameState& s, PlayerId p, int n) {
    auto& deck = s.player(p).main_deck;
    int burns = 0;
    for (int i = 0; i < n; ++i) {
        while (deck.empty()) {
            if (s.winner) return;
            burn_out(s, p, ++burns);
        }
        move_to(s, deck.back(), Zone::Trash, p);
    }
}

void burn_out(GameState& s, PlayerId p, int in_sequence) {
    recycle(s, s.player(p).trash);  // 432.2.b, random order (417.5)
    const PlayerId opp = opponent(p);  // 432.2.c: the only one to choose in a Duel
    const int points = ++s.player(opp).points;
    if (in_sequence > 1 && points >= victory_score && points > s.player(p).points) s.winner = opp;  // 432.3.c
}

void channel(GameState& s, PlayerId p, int n) {
    auto& deck = s.player(p).rune_deck;
    for (int i = 0; i < n && !deck.empty(); ++i) move_to(s, deck.back(), Zone::Base, p);  // ready: 431.2.a
}

void recycle(GameState& s, std::vector<ObjId> ids) {
    s.rng.shuffle(ids);
    for (const ObjId id : ids) {
        Object& o = s.obj(id);
        const Zone deck = o.card->type == CardType::Rune ? Zone::RuneDeck : Zone::MainDeck;
        move_to(s, id, deck, o.owner, 0, Pos::Bottom);
        if (ids.size() > 1) o.known = 0;
    }
}

std::vector<std::string> check_invariants(const GameState& s) {
    std::vector<std::string> problems;
    std::vector<int> seen(s.objects.size(), 0);
    const auto check_list = [&](const std::vector<ObjId>& list, Zone zone, PlayerId player, std::uint8_t where,
                                const std::string& name) {
        for (const ObjId id : list) {
            if (id >= s.objects.size()) {
                problems.push_back(name + ": id " + std::to_string(id) + " out of range");
                continue;
            }
            ++seen[id];
            const Object& o = s.objects[id];
            const bool board = on_board(zone);
            if (o.zone != zone || (board ? o.controller : o.owner) != player || (o.where != where && board)) {
                problems.push_back(name + ": " + o.card->name + " says it is in the " +
                                   std::string(to_string(o.zone)));
            }
            if (!board && o.controller != o.owner) problems.push_back(name + ": " + o.card->name + " controlled");
            if ((is_public(zone) && o.known != everyone) || (zone == Zone::Hand && !(o.known & 1u << o.owner))) {
                problems.push_back(name + ": " + o.card->name + " not known to who sees it (128)");
            }
        }
    };
    for (PlayerId p = 0; p < player_count; ++p) {
        const Player& pl = s.player(p);
        const std::string who = "player " + std::to_string(p) + " ";
        check_list(pl.main_deck, Zone::MainDeck, p, 0, who + "main deck");
        check_list(pl.rune_deck, Zone::RuneDeck, p, 0, who + "rune deck");
        check_list(pl.hand, Zone::Hand, p, 0, who + "hand");
        check_list(pl.trash, Zone::Trash, p, 0, who + "trash");
        check_list(pl.banishment, Zone::Banishment, p, 0, who + "banishment");
        check_list(pl.champion, Zone::ChampionZone, p, 0, who + "champion zone");
        check_list(pl.legend, Zone::LegendZone, p, 0, who + "legend zone");
        check_list(pl.base, Zone::Base, p, 0, who + "base");
        if (pl.champion.size() > 1) problems.push_back(who + "champion zone holds " + std::to_string(pl.champion.size()));
        if (pl.legend.size() != 1) problems.push_back(who + "legend zone holds " + std::to_string(pl.legend.size()));
        if (pl.points < 0) problems.push_back(who + "has negative points");  // 194.4
        const Pool& pool = pl.pool;
        if (pool.energy < 0 || pool.universal < 0 ||
            std::any_of(pool.power.begin(), pool.power.end(), [](int v) { return v < 0; })) {
            problems.push_back(who + "has a negative rune pool");
        }
    }
    std::vector<ObjId> active;
    for (ObjId id = 0; id < s.objects.size(); ++id) {
        if (active_zone(s.objects[id].zone)) active.push_back(id);
    }
    if (active != s.active) problems.push_back("active list differs from the objects' zones");
    for (std::size_t b = 0; b < s.battlefields.size(); ++b) {
        const Battlefield& bf = s.battlefields[b];
        const std::string name = "battlefield " + std::to_string(b);
        for (PlayerId p = 0; p < player_count; ++p) {
            // per player: check_list compares the controller, so split the list
            std::vector<ObjId> mine;
            for (const ObjId id : bf.here) {
                if (id < s.objects.size() && s.objects[id].controller == p) mine.push_back(id);
            }
            check_list(mine, Zone::Battlefield, p, static_cast<std::uint8_t>(b), name);
            mine.clear();
            for (const ObjId id : bf.facedown) {
                if (id < s.objects.size() && s.objects[id].controller == p) mine.push_back(id);
            }
            check_list(mine, Zone::Facedown, p, static_cast<std::uint8_t>(b), name + " facedown");
        }
        if (bf.facedown.size() > 1) problems.push_back(name + ": facedown zone holds more than one card (107.3.b)");
        // the battlefield card itself: in no list, Battlefield::card is its place
        if (bf.card >= s.objects.size()) {
            problems.push_back(name + ": no card");
        } else {
            ++seen[bf.card];
            const Object& o = s.objects[bf.card];
            if (o.zone != Zone::Battlefield || o.where != b || o.card->type != CardType::Battlefield) {
                problems.push_back(name + ": its card " + o.card->name + " is out of place");
            }
        }
    }
    for (const ChainItem& item : s.chain) {
        if (item.card == no_obj) continue;
        if (item.card >= s.objects.size() || s.objects[item.card].zone != Zone::Chain) {
            problems.push_back("chain item " + std::to_string(item.card) + " is not a card on the chain");
            continue;
        }
        ++seen[item.card];
    }
    for (std::size_t id = 0; id < s.objects.size(); ++id) {
        const Object& o = s.objects[id];
        const int want = o.zone == Zone::Removed ? 0 : 1;
        if (seen[id] != want) {
            problems.push_back(o.card->name + " (" + std::string(to_string(o.zone)) + ") is in " +
                               std::to_string(seen[id]) + " zone lists");
        }
    }
    for (const Object& o : s.objects) {  // 719.3: attached gear sits with its top-most unit
        if (o.attached == no_obj) continue;
        const Object* top = o.attached < s.objects.size() ? &s.objects[o.attached] : nullptr;
        if (top == nullptr || top->card->type != CardType::Unit || !on_board(top->zone) || !on_board(o.zone) ||
            top->zone != o.zone || top->where != o.where || o.card->type == CardType::Unit) {
            problems.push_back(o.card->name + " attached out of place");
        }
    }
    const Decision& d = s.decision;
    if (d.kind != DecisionKind::None && s.winner) problems.push_back("decision open after the game ended");
    if (d.player >= player_count) problems.push_back("decision for player " + std::to_string(d.player));
    for (std::size_t i = 0; i < s.chain.size(); ++i) {
        const ChainItem& item = s.chain[i];
        if (item.card < s.objects.size() && item.finalized && s.objects[item.card].card->type != CardType::Spell) {
            problems.push_back("finalized permanent still on the chain (337.2)");
        }
        if (!item.finalized && i + 1 < s.chain.size() && s.chain[i + 1].finalized) {
            problems.push_back("pending chain item below a finalized one (337.1.b)");
        }
    }
    if (std::count_if(s.chain.begin(), s.chain.end(), [](const ChainItem& c) { return c.resolving; }) > 1) {
        problems.push_back("two chain items resolving at once");
    }
    for (const ChainItem& item : s.chain) {
        if (item.resolving && !item.finalized) problems.push_back("pending chain item resolving");
    }
    if (d.kind == DecisionKind::Choose) {
        if (d.min > d.max || d.max > d.options.size() || d.chosen.size() > d.max) {
            problems.push_back("choose decision out of bounds");
        }
        for (std::size_t i = 1; i < d.chosen.size(); ++i) {
            if (d.chosen[i] <= d.chosen[i - 1]) problems.push_back("choose picks out of order");
        }
    }
    if (d.kind == DecisionKind::Priority) {
        if (!s.triggered.empty()) problems.push_back("priority with triggers not on the chain (383.3)");
        if (std::any_of(s.chain.begin(), s.chain.end(), [](const ChainItem& c) { return c.resolving; })) {
            problems.push_back("priority while an item resolves");
        }
        if (s.chain.empty()) {
            if (!s.showdown && d.player != s.turn_player) problems.push_back("open-state priority off turn (316.5.b)");
            if (s.showdown && !s.showdown->window) problems.push_back("open-state priority after the showdown closed");
            if (s.showdown && d.player != s.showdown->focus) problems.push_back("open-state priority without focus (347)");
        }
        if (!s.chain.empty() && d.player != chain_priority(s)) problems.push_back("priority with the wrong player");
        if (std::any_of(s.chain.begin(), s.chain.end(), [](const ChainItem& c) { return !c.finalized; })) {
            problems.push_back("priority while a chain item is pending (337.1)");
        }
        if (s.passes >= player_count) problems.push_back("priority after everyone passed (339.1)");
    }
    const bool resolving = std::any_of(s.chain.begin(), s.chain.end(), [](const ChainItem& c) { return c.resolving; });
    if (d.kind != DecisionKind::None && s.cleanup_due && !resolving) {  // 321: none inside a resolution
        problems.push_back("decision open before a due cleanup");
    }
    if (!s.winner && d.kind == DecisionKind::None) problems.push_back("nothing to decide in a running game");
    // showdowns and combat (341–348, 461–468); designations are fixed by the next cleanup (323.2)
    if (s.showdown) {
        const Showdown& sd = *s.showdown;
        if (sd.where >= s.battlefields.size()) {
            problems.push_back("showdown at no battlefield");
        } else if (sd.combat && !s.cleanup_due) {
            for (std::size_t id = 0; id < s.objects.size(); ++id) {
                const Object& o = s.objects[id];
                if (!on_board(o.zone) || o.card->type != CardType::Unit) continue;
                const bool here = o.zone == Zone::Battlefield && o.where == sd.where;
                const Designation want = !here                        ? Designation::None
                                         : o.controller == sd.attacker ? Designation::Attacker
                                                                       : Designation::Defender;
                if (o.designation != want) problems.push_back(o.card->name + " has the wrong combat designation");
            }
        }
    } else {
        for (const Object& o : s.objects) {
            if (o.designation != Designation::None) problems.push_back(o.card->name + " designated outside combat");
        }
    }
    for (std::size_t b = 0; b < s.battlefields.size(); ++b) {  // 323.5: lost in the next open cleanup
        const Battlefield& bf = s.battlefields[b];
        if (!bf.controller || s.cleanup_due || closed(s) || (s.showdown && s.showdown->where == b)) continue;
        const bool held = std::any_of(bf.here.begin(), bf.here.end(), [&](ObjId id) {
            return s.objects[id].controller == *bf.controller && s.objects[id].card->type == CardType::Unit;
        });
        if (!held) problems.push_back("battlefield " + std::to_string(b) + " controlled without units (323.5)");
    }
    return problems;
}

} // namespace buddy::riftbound
