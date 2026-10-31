#include "engine.hpp"

#include <algorithm>

namespace buddy::riftbound {

namespace {

bool is_basic_rune(const Card& card) {
    if (card.type != CardType::Rune) return false;
    for (const char* name : {"Fury Rune", "Calm Rune", "Mind Rune", "Body Rune", "Chaos Rune", "Order Rune"}) {
        if (card.name == name) return true;
    }
    return false;
}

ObjId add_object(GameState& s, const Card* card, PlayerId owner, Zone zone, std::uint8_t where = 0) {
    const auto id = static_cast<ObjId>(s.objects.size());
    if (id == no_obj) throw std::logic_error("too many objects");
    Object& o = s.objects.emplace_back();
    o.card = card;
    o.owner = o.controller = owner;
    o.zone = zone;
    o.where = where;
    if (card->type == CardType::Battlefield) return id;  // its place is Battlefield::card (or Removed), no list
    if (auto* list = zone_list(s, zone, owner, where)) list->push_back(id);
    return id;
}

void check_implemented(const Deck& seat0, const Deck& seat1) {
    std::vector<std::string> missing;
    const auto need = [&](const Card* c) {
        if (!is_implemented(*c) && std::find(missing.begin(), missing.end(), c->name) == missing.end()) {
            missing.push_back(c->name);
        }
    };
    for (const Deck* d : {&seat0, &seat1}) {
        need(d->legend);
        need(d->champion);
        for (const auto& e : d->main) need(e.card);
        for (const auto& e : d->runes) need(e.card);
        for (const Card* b : d->battlefields) need(b);
    }
    if (missing.empty()) return;
    std::string list;
    for (const auto& name : missing) list += (list.empty() ? "" : ", ") + name;
    throw std::runtime_error(std::to_string(missing.size()) + " cards not implemented (D21; vanilla mode is for " +
                             "tests): " + list);
}

// 110–114 + 487.5 for one player.
void setup_player(GameState& s, PlayerId p, const Deck& deck) {
    if (deck.legend == nullptr || deck.champion == nullptr || deck.battlefields.empty()) {
        throw std::invalid_argument("deck without legend, chosen champion or battlefields");
    }
    add_object(s, deck.legend, p, Zone::LegendZone);       // 111
    add_object(s, deck.champion, p, Zone::ChampionZone);   // 112
    for (const auto& e : deck.main) {
        for (int i = 0; i < e.count; ++i) add_object(s, e.card, p, Zone::MainDeck);
    }
    for (const auto& e : deck.runes) {
        for (int i = 0; i < e.count; ++i) add_object(s, e.card, p, Zone::RuneDeck);
    }
    s.rng.shuffle(s.player(p).main_deck);  // 114
    s.rng.shuffle(s.player(p).rune_deck);

    // 113 + 487.5: one battlefield at random, the others leave the game
    const auto chosen = s.rng.below(deck.battlefields.size());
    for (std::size_t i = 0; i < deck.battlefields.size(); ++i) {
        if (i != chosen) {
            add_object(s, deck.battlefields[i], p, Zone::Removed);
            continue;
        }
        const auto where = static_cast<std::uint8_t>(s.battlefields.size());
        s.battlefields.emplace_back().card = add_object(s, deck.battlefields[i], p, Zone::Battlefield, where);
    }
}

void push_tasks(GameState& s, std::initializer_list<Task> in_order) {
    for (auto it = std::rbegin(in_order); it != std::rend(in_order); ++it) s.tasks.push_back(*it);
}

void run_task(GameState& s, const Task& task) {
    switch (task.kind) {
        case TaskKind::Mulligan:  // 117
            s.decision = {DecisionKind::Mulligan, task.player, {}};
            return;
    }
}

// Runs tasks until a decision is open, the game is over, or nothing is left. Turn structure (#22) continues here.
void run(GameState& s) {
    while (s.decision.kind == DecisionKind::None && !s.winner && !s.tasks.empty()) {
        const Task task = s.tasks.back();
        s.tasks.pop_back();
        run_task(s, task);
    }
}

std::size_t hand_index(const GameState& s, PlayerId p, ObjId id) {
    const auto& hand = s.player(p).hand;
    return static_cast<std::size_t>(std::find(hand.begin(), hand.end(), id) - hand.begin());
}

// 117: set aside, draw as many, recycle the set-aside cards.
void finish_mulligan(GameState& s) {
    const Decision d = s.decision;
    s.decision = {};
    for (const ObjId id : d.picked) move_to(s, id, Zone::Removed, d.player);  // set aside: in no zone for a moment
    draw(s, d.player, static_cast<int>(d.picked.size()));
    recycle(s, d.picked);
}

} // namespace

bool is_implemented(const Card& card) { return is_basic_rune(card); }

GameState new_game(const Deck& seat0, const Deck& seat1, const GameOptions& options) {
    if (!options.vanilla) check_implemented(seat0, seat1);
    GameState s;
    s.rng = Rng(options.seed);
    setup_player(s, 0, seat0);
    setup_player(s, 1, seat1);
    s.first_player = static_cast<PlayerId>(s.rng.below(player_count));  // 115
    s.turn_player = s.first_player;
    const PlayerId second = opponent(s.first_player);
    draw(s, s.first_player, opening_hand);  // 116
    draw(s, second, opening_hand);
    push_tasks(s, {{TaskKind::Mulligan, s.first_player}, {TaskKind::Mulligan, second}});
    run(s);
    return s;
}

std::vector<Action> legal_actions(const GameState& s) {
    std::vector<Action> actions;
    const Decision& d = s.decision;
    switch (d.kind) {
        case DecisionKind::None: break;
        case DecisionKind::Mulligan: {
            if (d.picked.size() < max_mulligan) {
                const auto& hand = s.player(d.player).hand;
                const std::size_t from = d.picked.empty() ? 0 : hand_index(s, d.player, d.picked.back()) + 1;
                for (std::size_t i = from; i < hand.size(); ++i) actions.push_back({ActionKind::Pick, hand[i]});
            }
            actions.push_back({ActionKind::Done});
            break;
        }
    }
    return actions;
}

void apply(GameState& s, const Action& action) {
    const auto legal = legal_actions(s);
    if (std::find(legal.begin(), legal.end(), action) == legal.end()) {
        throw IllegalAction("not legal now (" + std::string(to_string(s.decision.kind)) + "): " +
                            describe(s, action));
    }
    switch (s.decision.kind) {
        case DecisionKind::None: break;
        case DecisionKind::Mulligan:
            if (action.kind == ActionKind::Pick) {
                s.decision.picked.push_back(action.obj);
                return;
            }
            finish_mulligan(s);
            break;
    }
    run(s);
}

std::string describe(const GameState& s, const Action& action) {
    const std::string name = action.obj < s.objects.size() ? s.obj(action.obj).card->name : "?";
    switch (action.kind) {
        case ActionKind::Pick:
            return s.decision.kind == DecisionKind::Mulligan ? "set aside " + name : "pick " + name;
        case ActionKind::Done:
            return s.decision.kind == DecisionKind::Mulligan && s.decision.picked.empty() ? "keep" : "done";
    }
    return "?";
}

} // namespace buddy::riftbound
