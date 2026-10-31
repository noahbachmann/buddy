#include "engine.hpp"

#include <algorithm>

#include "pay.hpp"

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

void start_turn(GameState& s) {
    const PlayerId p = s.turn_player;
    push_tasks(s, {{TaskKind::Awaken, p}, {TaskKind::Beginning, p}, {TaskKind::Channel, p}, {TaskKind::Draw, p},
                   {TaskKind::Main, p}});
}

void enter_phase(GameState& s, Phase phase) {
    s.phase = phase;
    s.cleanup_due = true;  // 319.2
}

void empty_pools(GameState& s) {  // 167
    for (Player& pl : s.players) pl.pool = {};
}

// 323. Until #23: the win check (323.1); the board steps come with combat and the chain.
void cleanup(GameState& s) {
    s.cleanup_due = false;
    for (PlayerId p = 0; p < player_count; ++p) {
        const int points = s.player(p).points;
        if (points >= victory_score && points > s.player(opponent(p)).points) s.winner = p;  // 194.2
    }
}

void run_task(GameState& s, const Task& task) {
    const PlayerId p = task.player;
    switch (task.kind) {
        case TaskKind::Mulligan:  // 117
            s.decision = {DecisionKind::Mulligan, p, {}};
            return;
        case TaskKind::Awaken:  // 315.1: ready everything p controls on the board (416.3.a)
            ++s.turn;
            enter_phase(s, Phase::Awaken);
            for (Object& o : s.objects) {
                if (on_board(o.zone) && o.controller == p && o.card->type != CardType::Battlefield) {
                    o.exhausted = false;
                }
            }
            return;
        case TaskKind::Beginning:  // 315.2: beginning-step effects (#24); scoring step: Hold (469) comes with #25
            enter_phase(s, Phase::Beginning);
            return;
        case TaskKind::Channel:  // 315.3, 487.7: the second player's first turn channels one more
            enter_phase(s, Phase::Channel);
            channel(s, p, channel_per_turn + (s.turn == 2 ? 1 : 0));
            return;
        case TaskKind::Draw:  // 315.4, Burn Out inside
            enter_phase(s, Phase::Draw);
            draw(s, p, 1);
            return;
        case TaskKind::Main:  // 316.3; start-of-main effects (#24)
            enter_phase(s, Phase::Main);
            empty_pools(s);
            s.tasks.push_back({TaskKind::Priority, p});
            return;
        case TaskKind::Priority:
            s.decision = {DecisionKind::Priority, p, {}};
            return;
        case TaskKind::Ending:  // 317: ending step effects (#24), then the Ending Special Cleanup
            enter_phase(s, Phase::Ending);
            for (Object& o : s.objects) {
                if (on_board(o.zone)) o.damage = 0;  // 317.2.b: heal all units
            }
            // 317.2.c: "this turn" effects expire with #24
            empty_pools(s);  // 317.2.d
            for (Player& pl : s.players) pl.scored = 0;
            s.turn_player = opponent(p);  // 317.3
            start_turn(s);
            return;
    }
}

// Runs a due cleanup before every task (319), tasks until a decision is open, the game is over or none are left.
void run(GameState& s) {
    while (!s.winner) {
        if (s.cleanup_due) {
            cleanup(s);
            continue;
        }
        if (s.decision.kind != DecisionKind::None || s.tasks.empty()) break;
        const Task task = s.tasks.back();
        s.tasks.pop_back();
        run_task(s, task);
    }
    if (s.winner) {
        s.decision = {};
        s.tasks.clear();
    }
}

// 359.2: paid, the permanent leaves the chain. Units enter exhausted, gear ready, both in the base until #25.
void finish_permanent(GameState& s) {
    const ChainItem item = s.chain.back();
    s.chain.pop_back();
    move_to(s, item.card, Zone::Base, item.controller);
    s.obj(item.card).exhausted = s.obj(item.card).card->type == CardType::Unit;
    s.tasks.push_back({TaskKind::Priority, item.controller});
}

// 357 for the card on top of the chain: picks the forced runes, stops at a Recycle decision while the domain is a
// choice, pays once the set is complete.
void continue_payment(GameState& s) {
    const ChainItem& item = s.chain.back();
    const Cost cost = printed_cost(*s.obj(item.card).card);
    auto& picked = s.decision.picked;
    for (;;) {
        const auto options = recycle_options(s, item.controller, cost, picked);
        if (options.empty()) break;
        if (options.size() > 1) return;
        picked.push_back(options.front());
    }
    const std::vector<ObjId> recycled = picked;
    s.decision = {};
    pay(s, item.controller, cost, recycled);
    finish_permanent(s);
}

// 353–359 for a permanent from hand: to the chain, cost, pay.
void play(GameState& s, ObjId id) {
    const PlayerId p = s.decision.player;
    move_to(s, id, Zone::Chain, p);  // 354
    s.chain.push_back({id, s.obj(id).ref(id), p, false});
    s.decision = {DecisionKind::Recycle, p, {}};
    continue_payment(s);
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
    start_turn(s);  // after the mulligans: the stack runs back first
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
        case DecisionKind::Priority:
            for (const ObjId id : s.player(d.player).hand) {
                if (playable(s, d.player, id)) actions.push_back({ActionKind::Play, id});
            }
            actions.push_back({ActionKind::EndTurn});
            break;
        case DecisionKind::Recycle: {
            const Cost cost = printed_cost(*s.obj(s.chain.back().card).card);
            for (const ObjId id : recycle_options(s, d.player, cost, d.picked)) {
                actions.push_back({ActionKind::Pick, id});
            }
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
        case DecisionKind::Priority:
            if (action.kind == ActionKind::Play) {
                play(s, action.obj);
            } else {
                s.decision = {};
                s.tasks.push_back({TaskKind::Ending, s.turn_player});  // 316.9
            }
            break;
        case DecisionKind::Recycle:
            s.decision.picked.push_back(action.obj);
            continue_payment(s);
            break;
    }
    run(s);
}

void concede(GameState& s, PlayerId p) {
    if (s.winner) return;
    s.winner = opponent(p);  // 651.1
    s.decision = {};
    s.tasks.clear();
}

bool playable(const GameState& s, PlayerId p, ObjId id) {
    const Card& card = *s.obj(id).card;
    return (card.type == CardType::Unit || card.type == CardType::Gear) && can_pay(s, p, printed_cost(card));
}

std::string describe(const GameState& s, const Action& action) {
    const std::string name = action.obj < s.objects.size() ? s.obj(action.obj).card->name : "?";
    switch (action.kind) {
        case ActionKind::Pick:
            if (s.decision.kind == DecisionKind::Mulligan) return "set aside " + name;
            if (s.decision.kind == DecisionKind::Recycle) return "recycle " + name;
            return "pick " + name;
        case ActionKind::Play: return "play " + name;
        case ActionKind::EndTurn: return "end turn";
        case ActionKind::Done:
            return s.decision.kind == DecisionKind::Mulligan && s.decision.picked.empty() ? "keep" : "done";
    }
    return "?";
}

} // namespace buddy::riftbound
