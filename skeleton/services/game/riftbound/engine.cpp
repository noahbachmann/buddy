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

// A cleanup (323), in the rule's order. Steps for showdowns and combat (323.2, 323.7–323.13) get their logic with
// #25. `ending` = the Ending Special Cleanup (317.2). Whatever changes re-arms cleanup_due (322; move_to does so for
// cards leaving the board), so the engine repeats cleanups until one changes nothing.
void cleanup(GameState& s, bool ending = false) {
    s.cleanup_due = false;
    for (PlayerId p = 0; p < player_count; ++p) {  // 323.1
        const int points = s.player(p).points;
        if (points >= victory_score && points > s.player(opponent(p)).points) {  // 194.2
            s.winner = p;
            return;
        }
    }
    // 323.2: combat designations (#25)

    // 323.4 (3a): lethal damage kills (429: to the owner's trash; death triggers with #24)
    std::vector<ObjId> dead;
    for (ObjId id = 0; id < s.objects.size(); ++id) {
        const Object& o = s.obj(id);
        if (on_board(o.zone) && o.card->type == CardType::Unit && lethal_damage(s, id)) dead.push_back(id);
    }
    for (const ObjId id : dead) move_to(s, id, Zone::Trash, s.obj(id).owner);
    if (ending) {
        for (Object& o : s.objects) {
            if (on_board(o.zone)) o.damage = 0;  // 317.2.b (3c): heal all units
        }
        // 317.2.c (3d): "this turn" effects expire (#24)
        for (Player& pl : s.players) pl.scored = 0;
        empty_pools(s);  // 317.2.d (3e)
    }

    // 323.5 (4): a controlled battlefield without its controller's units, in an open state without a showdown there
    if (!closed(s) && !s.showdown) {
        for (Battlefield& bf : s.battlefields) {
            if (!bf.controller) continue;
            const bool held = std::any_of(bf.here.begin(), bf.here.end(), [&](ObjId id) {
                return s.obj(id).controller == *bf.controller && s.obj(id).card->type == CardType::Unit;
            });
            if (!held) {
                bf.controller.reset();
                s.cleanup_due = true;  // 319.7
            }
        }
    }
    // 323.6 (5): recall gear and runes at battlefields (attached gear stays: #26). A card in another player's base
    // can't happen here: a base list is its controller's.
    for (std::size_t b = 0; b < s.battlefields.size(); ++b) {
        const std::vector<ObjId> here = s.battlefields[b].here;
        for (const ObjId id : here) {
            const Object& o = s.obj(id);
            if (o.card->type == CardType::Unit) continue;
            move_to(s, id, Zone::Base, o.controller);
            s.cleanup_due = true;  // 319.8
        }
        // 323.6.a: hidden cards at a battlefield their controller doesn't control
        const std::vector<ObjId> facedown = s.battlefields[b].facedown;
        for (const ObjId id : facedown) {
            if (s.battlefields[b].controller != s.obj(id).controller) move_to(s, id, Zone::Trash, s.obj(id).owner);
        }
    }
    // 323.7–323.13 (6–10): staging showdowns and combats, Contested, the turn player's pick (#25)
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
        case TaskKind::Main:  // 316.3; start-of-main effects (#24). Priority follows from 335 in run()
            enter_phase(s, Phase::Main);
            empty_pools(s);
            return;
        case TaskKind::Ending:  // 317.1: ending step effects (#24)
            enter_phase(s, Phase::Ending);
            s.tasks.push_back({TaskKind::Expiration, p});
            return;
        case TaskKind::Expiration:  // 317.2; 317.2.f (repeat after FEPR) once triggers exist (#24)
            cleanup(s, true);
            if (s.winner) return;
            s.turn_player = opponent(p);  // 317.3
            start_turn(s);
            return;
    }
}

std::size_t pending_index(const GameState& s) {
    const auto it = std::find_if(s.chain.begin(), s.chain.end(), [](const ChainItem& c) { return !c.finalized; });
    return static_cast<std::size_t>(it - s.chain.begin());
}

// 337.1, 355–359 for the pending item at `i`: choices, cost, payment, legality, then finished. Stops at a Recycle
// decision while the domain is a choice; the answer calls it again.
void finalize(GameState& s, std::size_t i) {
    const ChainItem item = s.chain.at(i);
    // 355: targets and modes (#24), a unit's location (#25)
    const Cost cost = printed_cost(*s.obj(item.card).card);  // 356: modifiers with #24
    if (s.decision.kind != DecisionKind::Recycle) s.decision = {DecisionKind::Recycle, item.controller, {}};
    auto& picked = s.decision.picked;
    for (;;) {  // 357
        const auto options = recycle_options(s, item.controller, cost, picked);
        if (options.empty()) break;
        if (options.size() > 1) return;
        picked.push_back(options.front());
    }
    const std::vector<ObjId> recycled = picked;
    s.decision = {};
    pay(s, item.controller, cost, recycled);
    // 358: legal_actions only offers legal plays, so the check holds and nothing is ever undone (358.5)
    s.chain[i].finalized = true;  // 359.1
    s.cleanup_due = true;         // 319.4
    s.passes = 0;
    if (s.obj(item.card).card->type == CardType::Spell) return;  // 359.3: waits on the chain
    // 337.2, 359.2: a permanent resolves at once. Units enter exhausted, gear ready, both in the base until #25
    s.chain.erase(s.chain.begin() + static_cast<std::ptrdiff_t>(i));
    move_to(s, item.card, Zone::Base, item.controller);
    s.obj(item.card).exhausted = s.obj(item.card).card->type == CardType::Unit;
}

// 340.1, 359.3.d: the newest item resolves, then the spell goes to its owner's trash. Its effects come with 5.3; a
// spell with nothing it can do still resolves (055.1), like every vanilla spell.
void resolve(GameState& s) {
    const ChainItem item = s.chain.back();
    s.chain.pop_back();
    move_to(s, item.card, Zone::Trash, s.obj(item.card).owner);
    s.cleanup_due = true;  // 319.5
    s.passes = 0;          // 340.4: the newest item's controller next
}

// HOT FEPR (334): a due cleanup before anything else (319), then tasks, then Finalize the oldest pending item,
// Execute (priority), Pass, Resolve; with no chain, the turn player's main-phase priority (335). Stops at a decision,
// the game's end or when nothing is left.
void run(GameState& s) {
    while (!s.winner) {
        if (s.cleanup_due) {
            cleanup(s);
            continue;
        }
        if (s.decision.kind != DecisionKind::None) break;
        if (!s.tasks.empty()) {
            const Task task = s.tasks.back();
            s.tasks.pop_back();
            run_task(s, task);
            continue;
        }
        if (const auto i = pending_index(s); i < s.chain.size()) {
            finalize(s, i);
            continue;
        }
        if (!s.chain.empty()) {
            if (s.passes >= player_count) {  // 339.1
                resolve(s);
                continue;
            }
            s.decision = {DecisionKind::Priority, chain_priority(s), {}};  // 337.4, 339.2, 340.4
            break;
        }
        if (s.phase == Phase::Main && !s.showdown) s.decision = {DecisionKind::Priority, s.turn_player, {}};
        break;  // showdowns' focus (335.1) with #25
    }
    if (s.winner) {
        s.decision = {};
        s.tasks.clear();
    }
}

// 354: a card from hand to the chain as a pending item; run() finalizes it.
void play(GameState& s, ObjId id) {
    const PlayerId p = s.decision.player;
    s.decision = {};
    move_to(s, id, Zone::Chain, p);
    s.chain.push_back({id, s.obj(id).ref(id), p, false});
    s.cleanup_due = true;  // 319.3
    s.passes = 0;          // 339.1: an item was added
}

// 308.1.a, 309.1.a, 310.1.a: what the turn's state lets a card be played with. Neutral Open only reaches here with
// the turn player's main-phase priority.
bool timing_allows(const GameState& s, Timing timing) {
    switch (turn_state(s)) {
        case TurnState::NeutralOpen: return true;
        case TurnState::ShowdownOpen: return timing != Timing::Normal;
        case TurnState::NeutralClosed:
        case TurnState::ShowdownClosed: return timing == Timing::Reaction;
    }
    return false;
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
            actions.push_back({closed(s) ? ActionKind::Pass : ActionKind::EndTurn});
            break;
        case DecisionKind::Recycle: {
            const Cost cost = printed_cost(*s.obj(s.chain.at(pending_index(s)).card).card);
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
            } else if (action.kind == ActionKind::Pass) {
                s.decision = {};
                ++s.passes;  // 338.1.b
            } else {
                s.decision = {};
                s.tasks.push_back({TaskKind::Ending, s.turn_player});  // 316.9
            }
            break;
        case DecisionKind::Recycle:
            s.decision.picked.push_back(action.obj);
            finalize(s, pending_index(s));
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
    const bool kind = card.type == CardType::Unit || card.type == CardType::Gear || card.type == CardType::Spell;
    return kind && timing_allows(s, card.timing) && can_pay(s, p, printed_cost(card));
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
        case ActionKind::Pass: return "pass";
        case ActionKind::Done:
            return s.decision.kind == DecisionKind::Mulligan && s.decision.picked.empty() ? "keep" : "done";
    }
    return "?";
}

} // namespace buddy::riftbound
