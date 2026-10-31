#include "engine.hpp"

#include <algorithm>

#include "actions.hpp"
#include "pay.hpp"
#include "rules.hpp"

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

    // 323.4 (3a): lethal damage kills (429.1.a.2), simultaneously; death triggers queue
    std::vector<ObjId> dead;
    for (ObjId id = 0; id < s.objects.size(); ++id) {
        const Object& o = s.obj(id);
        if (on_board(o.zone) && o.card->type == CardType::Unit && lethal_damage(s, id)) dead.push_back(id);
    }
    if (!dead.empty()) kill_all(s, dead, {});
    if (ending) {
        for (Object& o : s.objects) {
            if (on_board(o.zone)) o.damage = 0;  // 317.2.b (3c): heal all units
        }
        expire(s, Duration::Turn);  // 317.2.c (3d)
        for (Player& pl : s.players) pl.scored = 0;
        empty_pools(s);  // 317.2.d (3e)
    }
    prune(s);  // effects and delayed abilities whose objects are gone

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

void phase_start(GameState& s, Phase phase) {  // "at the start of …" (315.2.a, 316) and "at end of turn" (317.1.a)
    Event e = make_event(EventKind::PhaseStart, {}, s.turn_player);
    e.amount = static_cast<std::int16_t>(phase);
    emit(s, e);
}

void run_task(GameState& s, const Task& task) {
    const PlayerId p = task.player;
    switch (task.kind) {
        case TaskKind::Mulligan:  // 117
            s.decision = decide(DecisionKind::Mulligan, p);
            return;
        case TaskKind::Awaken:  // 315.1: ready everything p controls on the board (416.3.a)
            ++s.turn;
            s.uses.clear();  // "each turn" counts (386)
            s.deaths_this_turn = 0;
            enter_phase(s, Phase::Awaken);
            for (Object& o : s.objects) {
                if (on_board(o.zone) && o.controller == p && o.card->type != CardType::Battlefield) {
                    o.exhausted = false;
                }
            }
            return;
        case TaskKind::Beginning:  // 315.2.a; scoring step: Hold (469) comes with #25
            enter_phase(s, Phase::Beginning);
            phase_start(s, Phase::Beginning);
            return;
        case TaskKind::Channel:  // 315.3, 487.7: the second player's first turn channels one more
            enter_phase(s, Phase::Channel);
            channel(s, p, channel_per_turn + (s.turn == 2 ? 1 : 0));
            return;
        case TaskKind::Draw:  // 315.4, Burn Out inside
            enter_phase(s, Phase::Draw);
            draw(s, p, 1);
            return;
        case TaskKind::Main:  // 316.3. Priority follows from 335 in run()
            enter_phase(s, Phase::Main);
            empty_pools(s);
            phase_start(s, Phase::Main);
            return;
        case TaskKind::Ending:  // 317.1: ending step effects
            enter_phase(s, Phase::Ending);
            phase_start(s, Phase::Ending);
            s.tasks.push_back({TaskKind::Expiration, p});
            return;
        case TaskKind::Expiration:  // 317.2
            cleanup(s, true);
            if (s.winner) return;
            if (!s.triggered.empty() || !s.chain.empty()) {  // 317.2.f: after the chain, start the step over
                s.tasks.push_back({TaskKind::Expiration, p});
                return;
            }
            s.turn_player = opponent(p);  // 317.3
            start_turn(s);
            return;
    }
}

std::size_t pending_index(const GameState& s) {
    const auto it = std::find_if(s.chain.begin(), s.chain.end(), [](const ChainItem& c) { return !c.finalized; });
    return static_cast<std::size_t>(it - s.chain.begin());
}

std::size_t resolving_index(const GameState& s) {
    const auto it = std::find_if(s.chain.begin(), s.chain.end(), [](const ChainItem& c) { return c.resolving; });
    return static_cast<std::size_t>(it - s.chain.begin());
}

void ask(GameState& s, PlayerId player, Purpose purpose, std::vector<Action> options, int min, int max) {
    s.decision = {};
    s.decision.kind = DecisionKind::Choose;
    s.decision.player = player;
    s.decision.purpose = purpose;
    s.decision.options = std::move(options);
    s.decision.min = static_cast<std::uint8_t>(min);
    s.decision.max = static_cast<std::uint8_t>(max);
}

std::vector<Action> take_answer(GameState& s) {
    auto a = std::move(*s.answer);
    s.answer.reset();
    return a;
}

// Takes item i off the chain without it ever being finalized (383.3.a.2, 403.4, 405.2): not countered.
void remove_item(GameState& s, std::size_t i) {
    s.chain.erase(s.chain.begin() + static_cast<std::ptrdiff_t>(i));
    s.cleanup_due = true;
}

// Finalize steps of a pending item (355–359, 402–406), in ChainItem::step.
enum FinStep : std::uint8_t { FinMay, FinTargets, FinOptional, FinCost, FinDiscard, FinRecycleTrash, FinRunes };

// 359: finished. Targeting effects trigger, permanents enter the board, spells and abilities wait for the passes.
void finished(GameState& s, std::size_t i) {
    ChainItem& item = s.chain[i];
    item.finalized = true;  // 359.1
    item.step = 0;
    item.sub = 0;
    s.cleanup_due = true;   // 319.4
    s.passes = 0;
    const ChainItem done = item;
    emit_targeted(s, done);  // 383.4.b
    if (done.kind != ItemKind::Card) {
        const AbilitySpec* spec = ability_of(s, done);
        if (done.kind == ItemKind::Activated && spec && spec->per_turn > 0) use(s, done.source, done.ability);
        if (spec && spec->add) s.chain[i].resolving = true;  // 401.2: resolves at once
        return;
    }
    const Object& card = s.obj(done.card);
    const Event played = make_event(EventKind::Played, done.source, done.controller);
    if (card.card->type == CardType::Spell) {  // 359.3: waits on the chain
        emit(s, played);
        return;
    }
    // 337.2, 359.2: a permanent resolves at once. Units enter exhausted unless they enter ready (369.3), gear ready;
    // both in the base until #25 (355.2 locations)
    s.chain.erase(s.chain.begin() + static_cast<std::ptrdiff_t>(i));
    move_to(s, done.card, Zone::Base, done.controller);
    const CardRules* rules = rules_of(s, done.card);
    const bool ready = rules != nullptr && rules->enters_ready(s, done.card, done);
    s.obj(done.card).exhausted = s.obj(done.card).card->type == CardType::Unit && !ready;
    emit(s, make_event(EventKind::Played, s.obj(done.card).ref(done.card), done.controller));
}

// 337.1, 355–359 for the pending item at `i`: "may", targets, optional costs, total cost, non-standard costs,
// runes, then finished. Stops at each decision; the answer (GameState::answer, or a Recycle pick) calls it again.
void finalize(GameState& s, std::size_t i) {
    for (;;) {
        ChainItem& item = s.chain[i];
        const PlayerId p = item.controller;
        switch (item.step) {
            case FinMay: {  // 383.3.a, 403.1
                const AbilitySpec* spec = ability_of(s, item);
                if (item.kind == ItemKind::Triggered && spec && spec->may) {
                    if (!s.answer) {
                        ask(s, p, Purpose::May, {{ActionKind::Pick, item.source.id}}, 0, 1);
                        return;
                    }
                    if (take_answer(s).empty()) {
                        if (spec->per_turn > 0) use(s, item.source, item.ability, -1);  // 386.2.b: not performed
                        remove_item(s, i);
                        return;
                    }
                }
                item.step = FinTargets;
                item.sub = 0;
                continue;
            }
            case FinTargets: {  // 355.5–355.13, 403.2
                const auto specs = target_specs(s, item);
                if (item.sub >= specs.size()) {
                    item.step = FinOptional;
                    item.sub = 0;
                    continue;
                }
                if (s.answer) {
                    for (const Action& a : take_answer(s)) {
                        Target t{{}, a.n, item.sub};
                        if (a.obj != no_obj) t = {s.obj(a.obj).ref(a.obj), 0, item.sub};
                        item.targets.push_back(t);
                    }
                    ++item.sub;
                    continue;
                }
                auto options = candidates(s, item, item.sub);
                const TargetSpec& spec = specs[item.sub];
                if (options.size() < spec.min) {
                    // 403.4: a trigger without legal choices leaves; a play or activation was offered only with them
                    if (item.kind != ItemKind::Triggered) throw std::logic_error("finalize: no legal targets");
                    remove_item(s, i);
                    return;
                }
                const int max = std::min<int>(spec.max, static_cast<int>(options.size()));
                if (max == 0 || static_cast<int>(options.size()) == spec.min) {  // nothing to decide
                    s.answer = max == 0 ? std::vector<Action>{} : options;
                    continue;
                }
                ask(s, p, Purpose::Target, std::move(options), spec.min, max);
                return;
            }
            case FinOptional: {  // 355.1.a: only ones that keep the total payable (355.16)
                const CardRules* rules = item.kind == ItemKind::Card ? rules_of(s, item) : nullptr;
                const auto optional = rules ? rules->optional_costs() : std::span<const Cost>{};
                if (item.sub >= optional.size()) {
                    item.step = FinCost;
                    continue;
                }
                if (s.answer) {
                    if (!take_answer(s).empty()) item.paid = static_cast<std::uint8_t>(item.paid | (1u << item.sub));
                    ++item.sub;
                    continue;
                }
                ChainItem with = item;
                with.paid = static_cast<std::uint8_t>(with.paid | (1u << item.sub));
                if (!payable(s, with, total_cost(s, with))) {
                    ++item.sub;
                    continue;
                }
                ask(s, p, Purpose::OptionalCost, {{ActionKind::Pick, item.card, item.sub}}, 0, 1);
                return;
            }
            case FinCost:  // 356, 404
                item.cost = total_cost(s, item);
                if (!payable(s, item, item.cost)) {
                    if (item.kind != ItemKind::Triggered) throw std::logic_error("finalize: cost not payable");
                    remove_item(s, i);  // 405.2: a trigger whose cost can't be paid
                    return;
                }
                item.step = FinDiscard;
                continue;
            case FinDiscard:
            case FinRecycleTrash: {  // 357.2: costs with a choice of cards
                const bool disc = item.step == FinDiscard;
                const int n = disc ? item.cost.discard : item.cost.recycle_trash;
                const FinStep next = disc ? FinRecycleTrash : FinRunes;
                if (n == 0) {
                    item.step = next;
                    continue;
                }
                std::vector<ObjId> cards;
                if (s.answer) {
                    for (const Action& a : take_answer(s)) cards.push_back(a.obj);
                } else {
                    std::vector<Action> options;
                    for (const ObjId id : disc ? s.player(p).hand : s.player(p).trash) {
                        if (id != item.card) options.push_back({ActionKind::Pick, id});
                    }
                    std::sort(options.begin(), options.end(), [](const Action& a, const Action& b) { return a.obj < b.obj; });
                    if (static_cast<int>(options.size()) > n) {
                        ask(s, p, disc ? Purpose::Discard : Purpose::RecycleTrash, std::move(options), n, n);
                        return;
                    }
                    for (const Action& a : options) cards.push_back(a.obj);
                }
                const ObjRef by = item.source;  // the item may move in memory: events can't touch the chain
                if (disc) {
                    for (const ObjId id : cards) discard(s, id, {by, p});
                } else {
                    recycle(s, cards);
                }
                s.chain[i].step = next;
                continue;
            }
            case FinRunes: {  // 357, 405
                const Cost cost = item.cost;
                if (s.decision.kind != DecisionKind::Recycle) s.decision = decide(DecisionKind::Recycle, p);
                auto& picked = s.decision.picked;
                for (;;) {
                    const auto options = recycle_options(s, p, cost, picked);
                    if (options.empty()) break;
                    if (options.size() > 1) return;
                    picked.push_back(options.front());
                }
                const std::vector<ObjId> recycled = picked;
                s.decision = {};
                pay(s, p, cost, recycled);
                const ObjRef src = item.source;
                if (cost.exhaust_self) exhaust(s, src.id);
                if (cost.disempower_self) disempower(s, src.id);
                if (cost.kill_self) kill(s, src.id, {src, p});  // paid even if replaced (357.2.a)
                // 358: legal_actions only offers legal plays, so the check holds and nothing is ever undone (358.5)
                finished(s, i);
                return;
            }
        }
    }
}

// 340.1: the newest item starts resolving.
void start_resolving(GameState& s) {
    s.chain.back().resolving = true;
    s.chain.back().step = 0;
    s.passes = 0;
}

// 359.3.d, 402–406: one step of the resolving item's effect. Done → it leaves the chain (a spell to its owner's trash),
// unless the effect already took it off (359.3.e.15). A spell with nothing it can do still resolves (055.1).
void resolve_step(GameState& s) {
    const ChainItem& item = s.chain[resolving_index(s)];
    const CardRules* rules = rules_of(s, item);
    Resolution r(s);
    if (rules != nullptr && rules->resolve(r) == Status::Wait) {
        if (s.decision.kind == DecisionKind::None) throw std::logic_error("resolve: waits without a decision");
        return;
    }
    if (s.answer) throw std::logic_error("resolve: answer left over");
    s.cleanup_due = true;  // 319.5
    s.passes = 0;          // 340.4: the newest item's controller next
    const std::size_t i = resolving_index(s);
    if (i == s.chain.size()) return;
    const ChainItem done = s.chain[i];
    s.chain.erase(s.chain.begin() + static_cast<std::ptrdiff_t>(i));
    if (done.card != no_obj && s.obj(done.card).zone == Zone::Chain) {
        move_to(s, done.card, Zone::Trash, s.obj(done.card).owner);
    }
    if (done.kind == ItemKind::Activated) {  // 377.2.a
        Event e = make_event(EventKind::Activated, done.source, done.controller);
        e.amount = done.ability;
        emit(s, e);
    }
}

// 383.3, 383.3.d: queued triggers onto the chain as pending items, the turn player's first (they resolve last),
// each player ordering their own: one decision per placement while they have more than one.
void place_triggers(GameState& s) {
    for (int k = 0; k < player_count && !s.triggered.empty(); ++k) {
        const auto p = static_cast<PlayerId>((s.turn_player + k) % player_count);
        for (;;) {
            std::vector<std::size_t> mine;
            for (std::size_t i = 0; i < s.triggered.size(); ++i) {
                if (s.triggered[i].controller == p) mine.push_back(i);
            }
            if (mine.empty()) break;
            std::size_t next = mine.front();
            if (s.answer) {
                next = take_answer(s).front().n;
            } else if (mine.size() > 1) {
                std::vector<Action> options;
                for (const std::size_t i : mine) {
                    options.push_back({ActionKind::Pick, s.triggered[i].source.id, static_cast<std::uint8_t>(i)});
                }
                ask(s, p, Purpose::TriggerOrder, std::move(options), 1, 1);
                return;
            }
            const Trigger t = s.triggered.at(next);
            s.triggered.erase(s.triggered.begin() + static_cast<std::ptrdiff_t>(next));
            ChainItem item;
            item.source = t.source;
            item.controller = t.controller;
            item.kind = ItemKind::Triggered;
            item.ability = t.ability;
            item.event = t.event;
            s.chain.push_back(item);
            s.cleanup_due = true;  // 319.3
            s.passes = 0;
        }
    }
}

// HOT FEPR (334): a due cleanup first (319; never inside a resolution, 321), then the resolving item's next step,
// Finalize the oldest pending item, put queued triggers on the chain (402.2: after a resolution), Execute
// (priority), Pass, Resolve; then tasks; with no chain, the turn player's main-phase priority (335). Stops at a
// decision, the game's end or when nothing is left.
void run(GameState& s) {
    while (!s.winner) {
        const bool resolving = resolving_index(s) < s.chain.size();
        if (s.cleanup_due && !resolving) {
            cleanup(s);
            continue;
        }
        if (s.decision.kind != DecisionKind::None) break;
        if (resolving) {
            resolve_step(s);
            continue;
        }
        if (const auto i = pending_index(s); i < s.chain.size()) {
            finalize(s, i);
            continue;
        }
        if (!s.triggered.empty()) {
            place_triggers(s);
            continue;
        }
        if (!s.chain.empty()) {
            if (s.passes >= player_count) {  // 339.1
                start_resolving(s);
                continue;
            }
            s.decision = decide(DecisionKind::Priority, chain_priority(s));  // 337.4, 339.2, 340.4
            break;
        }
        if (!s.tasks.empty()) {
            const Task task = s.tasks.back();
            s.tasks.pop_back();
            run_task(s, task);
            continue;
        }
        if (s.phase == Phase::Main && !s.showdown) s.decision = decide(DecisionKind::Priority, s.turn_player);
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
    ChainItem item;
    item.card = id;
    item.source = s.obj(id).ref(id);
    item.controller = p;
    s.chain.push_back(item);
    s.cleanup_due = true;  // 319.3
    s.passes = 0;          // 339.1: an item was added
}

// 377.3, 402.1: an activated ability onto the chain as a pending item.
void activate(GameState& s, ObjId source, std::uint8_t ability) {
    const PlayerId p = s.decision.player;
    s.decision = {};
    ChainItem item;
    item.source = s.obj(source).ref(source);
    item.controller = p;
    item.kind = ItemKind::Activated;
    item.ability = ability;
    s.chain.push_back(item);
    s.cleanup_due = true;
    s.passes = 0;
}

// 308.1.a, 309.1.a, 310.1.a: what the turn's state lets a card or ability be used with. Neutral Open only reaches
// here with the turn player's main-phase priority (381.1 for activated abilities).
bool timing_allows(const GameState& s, Timing timing) {
    switch (turn_state(s)) {
        case TurnState::NeutralOpen: return true;
        case TurnState::ShowdownOpen: return timing != Timing::Normal;
        case TurnState::NeutralClosed:
        case TurnState::ShowdownClosed: return timing == Timing::Reaction;
    }
    return false;
}

// Every target slot has enough valid choices (355.8, 403.3).
bool targets_available(const GameState& s, const ChainItem& item) {
    const auto specs = target_specs(s, item);
    for (std::size_t k = 0; k < specs.size(); ++k) {
        if (candidates(s, item, k).size() < specs[k].min) return false;
    }
    return true;
}

// 376–381: `p` may activate ability `a` of `source` now.
bool activatable(const GameState& s, PlayerId p, ObjId source, std::uint8_t a) {
    const CardRules* rules = rules_of(s, source);
    const AbilitySpec& spec = rules->abilities()[a];
    if (spec.kind != AbilityKind::Activated || controller_of(s, source) != p) return false;
    if (!timing_allows(s, spec.timing)) return false;
    if (spec.usable != nullptr && !spec.usable(s, source)) return false;  // 377.2.b
    const ObjRef ref = s.obj(source).ref(source);
    if (spec.per_turn > 0 && uses(s, ref, a) >= spec.per_turn) return false;
    ChainItem item;
    item.source = ref;
    item.controller = p;
    item.kind = ItemKind::Activated;
    item.ability = a;
    return targets_available(s, item) && payable(s, item, total_cost(s, item));
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

// Choose: picks in option order, and only ones after which `min` stays reachable, so each set has one path.
std::vector<Action> choose_options(const Decision& d) {
    std::vector<Action> out;
    if (d.chosen.size() < d.max) {
        const std::size_t from = d.chosen.empty() ? 0 : d.chosen.back() + 1u;
        const std::size_t still = d.min > d.chosen.size() ? d.min - d.chosen.size() : 0;
        for (std::size_t i = from; i < d.options.size(); ++i) {
            if (d.options.size() - i - 1 + 1 >= still) out.push_back(d.options[i]);
        }
    }
    if (d.chosen.size() >= d.min) out.push_back({ActionKind::Done});
    return out;
}

} // namespace

bool is_implemented(const Card& card) { return is_basic_rune(card) || card.rules != nullptr; }

GameState new_game(const Deck& seat0, const Deck& seat1, const GameOptions& options) {
    if (!options.vanilla) check_implemented(seat0, seat1);
    GameState s;
    s.rng = Rng(options.seed);
    s.vanilla = options.vanilla;
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
            for (ObjId id = 0; id < s.objects.size(); ++id) {  // 380: on the board, and legends
                const Zone z = s.obj(id).zone;
                const CardRules* rules = rules_of(s, id);
                if (rules == nullptr || !(on_board(z) || z == Zone::LegendZone)) continue;
                for (std::size_t a = 0; a < rules->abilities().size(); ++a) {
                    const auto ab = static_cast<std::uint8_t>(a);
                    if (activatable(s, d.player, id, ab)) actions.push_back({ActionKind::Activate, id, ab});
                }
            }
            actions.push_back({closed(s) ? ActionKind::Pass : ActionKind::EndTurn});
            break;
        case DecisionKind::Recycle: {
            const Cost& cost = s.chain.at(pending_index(s)).cost;
            for (const ObjId id : recycle_options(s, d.player, cost, d.picked)) {
                actions.push_back({ActionKind::Pick, id});
            }
            break;
        }
        case DecisionKind::Choose:
            actions = choose_options(d);
            break;
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
            } else if (action.kind == ActionKind::Activate) {
                activate(s, action.obj, action.n);
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
        case DecisionKind::Choose: {
            Decision& d = s.decision;
            if (action.kind == ActionKind::Pick) {
                const auto it = std::find(d.options.begin(), d.options.end(), action);
                d.chosen.push_back(static_cast<std::uint8_t>(it - d.options.begin()));
                if (d.chosen.size() < d.max) return;  // more to pick
            }
            std::vector<Action> picks;
            for (const std::uint8_t i : d.chosen) picks.push_back(d.options[i]);
            s.answer = std::move(picks);
            s.decision = {};
            break;
        }
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
    if (!kind || !timing_allows(s, card.timing)) return false;
    const CardRules* rules = rules_of(s, id);
    if (rules != nullptr && !rules->can_play(s, id, p)) return false;
    ChainItem item;  // as it would be on the chain, no optional costs yet
    item.card = id;
    item.source = s.obj(id).ref(id);
    item.controller = p;
    return targets_available(s, item) && payable(s, item, total_cost(s, item));
}

std::string describe(const GameState& s, const Action& action) {
    const std::string name = action.obj < s.objects.size() ? s.obj(action.obj).card->name : "?";
    switch (action.kind) {
        case ActionKind::Pick:
            if (s.decision.kind == DecisionKind::Mulligan) return "set aside " + name;
            if (s.decision.kind == DecisionKind::Recycle) return "recycle " + name;
            if (action.obj == no_obj) return "pick player " + std::to_string(action.n);
            switch (s.decision.purpose) {
                case Purpose::May: return "use " + name + "'s ability";
                case Purpose::OptionalCost: return "pay " + name + "'s optional cost " + std::to_string(action.n);
                case Purpose::Discard: return "discard " + name;
                case Purpose::RecycleTrash: return "recycle " + name;
                case Purpose::TriggerOrder: return "next on the chain: " + name + "'s ability";
                case Purpose::Target: return "choose " + name;
                default: return "pick " + name;
            }
        case ActionKind::Play: return "play " + name;
        case ActionKind::Activate: return "use " + name + "'s ability " + std::to_string(action.n);
        case ActionKind::EndTurn: return "end turn";
        case ActionKind::Pass: return "pass";
        case ActionKind::Done:
            if (s.decision.kind == DecisionKind::Mulligan && s.decision.picked.empty()) return "keep";
            if (s.decision.kind == DecisionKind::Choose && s.decision.chosen.empty() &&
                (s.decision.purpose == Purpose::May || s.decision.purpose == Purpose::OptionalCost)) {
                return "decline";
            }
            return "done";
    }
    return "?";
}

} // namespace buddy::riftbound
