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

bool on_board(Zone zone) { return zone == Zone::Base || zone == Zone::Battlefield || zone == Zone::Facedown; }

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

std::string_view to_string(DecisionKind kind) {
    switch (kind) {
        case DecisionKind::None: return "none";
        case DecisionKind::Mulligan: return "mulligan";
        case DecisionKind::Priority: return "priority";
        case DecisionKind::Recycle: return "recycle";
    }
    return "?";
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
    if (auto* from = zone_list(s, o.zone, list_player(o), o.where)) {
        const auto it = std::find(from->begin(), from->end(), id);
        if (it == from->end()) throw std::logic_error("move_to: object not in its zone list");
        from->erase(it);
    }
    if (on_board(o.zone) != on_board(zone)) s.cleanup_due = true;  // 319.6
    if (!on_board(o.zone) || !on_board(zone)) {
        ++o.gen;
        o.damage = 0;
        o.buffs = 0;
        o.exhausted = o.stunned = o.facedown = false;
        o.designation = Designation::None;
        controller = on_board(zone) ? controller : o.owner;
    }
    o.zone = zone;
    o.controller = controller;
    o.where = where;
    if (auto* to = zone_list(s, zone, list_player(o), where)) {
        if (pos == Pos::Top) to->push_back(id);
        else to->insert(to->begin(), id);
    }
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
        const Object& o = s.obj(id);
        const Zone deck = o.card->type == CardType::Rune ? Zone::RuneDeck : Zone::MainDeck;
        move_to(s, id, deck, o.owner, 0, Pos::Bottom);
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
    const Decision& d = s.decision;
    if (d.kind != DecisionKind::None && s.winner) problems.push_back("decision open after the game ended");
    if (d.player >= player_count) problems.push_back("decision for player " + std::to_string(d.player));
    if (d.kind == DecisionKind::Priority && d.player != s.turn_player) problems.push_back("priority off turn");
    if (d.kind != DecisionKind::None && s.cleanup_due) problems.push_back("decision open before a due cleanup");
    if (!s.winner && d.kind == DecisionKind::None) problems.push_back("nothing to decide in a running game");
    return problems;
}

} // namespace buddy::riftbound
