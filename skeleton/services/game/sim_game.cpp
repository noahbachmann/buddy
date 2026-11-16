#include "sim_game.hpp"

#include <algorithm>
#include <map>
#include <tuple>

#include "public_state.hpp"
#include "riftbound/ai/greedy.hpp"
#include "riftbound/engine.hpp"
#include "riftbound/random_game.hpp"
#include "riftbound/replay.hpp"

namespace buddy::game {

using namespace riftbound;
using table::CommandKind;
using TPlace = table::Place;

namespace {

std::optional<TPlace> spot_of(const Object& o) {
    switch (o.zone) {
        case Zone::Base: return TPlace::Base;
        case Zone::Battlefield: return o.where == 0 ? TPlace::Bf0 : TPlace::Bf1;
        case Zone::Facedown: return o.where == 0 ? TPlace::Fd0 : TPlace::Fd1;
        case Zone::Chain: return TPlace::Chain;
        case Zone::Trash: return TPlace::Trash;
        case Zone::Banishment: return TPlace::Banish;
        case Zone::ChampionZone: return TPlace::Champion;
        case Zone::LegendZone: return TPlace::Legend;
        default: return std::nullopt;
    }
}

std::unique_ptr<ai::Player> or_greedy(std::unique_ptr<ai::Player> p, std::uint64_t seed) {
    if (p) return p;
    return std::make_unique<ai::GreedyPlayer>(seed);
}

SessionConfig session_config(const SimGameConfig& c) {
    return {.decks = c.decks, .deck_texts = c.deck_texts, .buddy = c.buddy, .seed = c.seed};
}

} // namespace

SimGame::SimGame(const CardDb& db_, SimGameConfig c)
    : db(db_), cfg(session_config(c)), sim({}, c.faults, c.sim_seed), wrapper(c.wrap ? c.wrap(sim) : nullptr),
      table(wrapper ? *wrapper : static_cast<table::Backend&>(sim), {}),
      buddy_ai(or_greedy(std::move(c.buddy_player), c.seed * 2 + 1)),
      dev(1 - c.buddy, or_greedy(std::move(c.dev_player), c.seed * 2 + 2), c.seed * 3 + 7, c.true_deck),
      session(std::make_unique<Session>(db, cfg)), leak_rng_(c.seed * 5 + 11) {
    buddy_ai->begin(cfg.buddy, cfg.decks[0], cfg.decks[1]);
}

bool SimGame::buddy_knows(const GameState& s, ObjId id) const { return s.obj(id).known & 1u << session->buddy(); }

// At Dev's priority: a card is offered iff Dev could still hold it (a copy Buddy hasn't seen) and it's playable from a
// hidden hand object bound to it.
std::string SimGame::candidates_problem() const {
    const GameState& s = session->state();
    if (s.decision.kind != DecisionKind::Priority) return {};
    ObjId h = no_obj;
    for (const ObjId id : s.player(dev_seat()).hand) {
        if (!buddy_knows(s, id)) h = id;
    }
    if (h == no_obj) return {};
    std::set<const Card*> pool, offered;
    for (ObjId id = 0; id < s.objects.size(); ++id) {
        const Object& o = s.obj(id);
        if (o.owner == dev_seat() && (o.zone == Zone::MainDeck || o.zone == Zone::Hand) && !buddy_knows(s, id) &&
            o.card->type != CardType::Rune) {
            pool.insert(o.card);
        }
    }
    for (const auto& o : session->dev_options()) {
        if (o.named && o.card && o.action.kind == ActionKind::Play) offered.insert(o.card);
    }
    for (const auto& e : cfg.decks[dev_seat()].main) {
        const bool is_offered = offered.contains(e.card);
        if (!pool.contains(e.card)) {
            if (is_offered) return e.card->name + " offered, but Buddy has seen every copy";
            continue;
        }
        GameState copy = s;
        if (!riftbound::bind(copy, h, e.card, session->buddy())) return "can't bind " + e.card->name;
        if (playable(copy, dev_seat(), h) != is_offered) {
            return e.card->name + (is_offered ? " offered, but not playable" : " playable, but not offered");
        }
    }
    return {};
}

void SimGame::execute(const table::Command& c) {
    if (c.kind == CommandKind::Setup) {
        table::SimSetup s{c.sim_deck, c.sim_runes, c.legend, c.champion, true};
        sim.configure(s);
    }
    if (before_cmd) before_cmd(c);
    const auto events = table.execute(c);
    if (after_cmd) after_cmd(c, events);
    for (const auto& e : events) session->on_table(e);
}

void SimGame::run() {
    if (session->status() == Status::Setup && session->stats().commands == 0) {
        session->start();
        dev.begin(session->state(), cfg.decks[0], cfg.decks[1]);
        const GameState& s = session->state();
        for (ObjId id = 0; id < s.objects.size(); ++id) known_before_.push_back(buddy_knows(s, id));
    }
    for (long guard = 0; guard < 2'000'000; ++guard) {
        if (stop_when && stop_when()) return;  // before take(): what's pending stays in the session
        if (leak_every > 0 && inputs_++ % leak_every == 0) {
            if (auto p = leak_problem(leak_rng_); !p.empty()) leaks.push_back(p);
        }
        Outputs o = session->take();
        voice.insert(voice.end(), o.voice.begin(), o.voice.end());
        if (session->status() == Status::Paused &&
            (pauses.empty() || session->stats().pauses != static_cast<long>(pauses.size()))) {
            pauses.push_back(session->pause());
        }
        check();
        bool acted = false;
        for (const auto& c : o.table) {
            execute(c);
            acted = true;
        }
        if (o.ai) {
            std::optional<Action> a;
            if (buddy_choice) a = buddy_choice(*o.ai);
            if (!a) a = buddy_ai->choose(o.ai->view, o.ai->actions);
            session->on_ai(o.ai->id, *a);
            acted = true;
        }
        if (acted) continue;
        switch (session->status()) {
            case Status::DevToAct: {
                if (check_candidates) {
                    if (auto p = candidates_problem(); !p.empty()) problems.push_back("candidates: " + p);
                }
                const auto choice = dev.choose(session->state(), session->dev_options());
                if (choice.named && choice.card) announced_.insert(choice.obj);
                session->on_dev(choice);
                break;
            }
            case Status::DevToReveal: {
                const DevPrompt p = *session->prompt();
                for (const ObjId id : p.objs) {
                    if (buddy_knows(session->state(), id) && p.kind != DevPrompt::Kind::Battlefield) {
                        problems.push_back("prompt for a card Buddy knows: " + std::to_string(id));
                    }
                    announced_.insert(id);
                }
                prompt_cards += static_cast<long>(p.objs.size());
                session->on_reveal(dev.reveal(p));
                break;
            }
            case Status::Paused:
                if (session->pause() == Pause::Desync || resumes_left-- <= 0) return;
                session->resume();
                break;
            case Status::Over: return;
            default:
                problems.push_back("stuck in " + std::string(to_string(session->status())));
                return;
        }
    }
    problems.push_back("no end");
}

// After every committed step: the engine's view of Buddy's cards = the belief = the truth.
void SimGame::check() {
    if (session->stats().steps == last_steps_ || session->status() == Status::Paused) return;
    last_steps_ = session->stats().steps;
    ++checked;
    if (auto p = compare(); !p.empty()) problems.push_back("step " + std::to_string(last_steps_) + ": " + p);
    if (auto p = dev_problem(); !p.empty()) problems.push_back("step " + std::to_string(last_steps_) + ": " + p);
}

// Dev's side: every card of Dev's that Buddy knows is SimDev's true one, and Buddy learned it from a prompt or Dev
// naming it, never on its own; a prompted card is known after its step.
std::string SimGame::dev_problem() {
    const GameState& s = session->state();
    known_before_.resize(s.objects.size(), 0);
    std::string problem;
    for (ObjId id = 0; id < s.objects.size() && problem.empty(); ++id) {
        const Object& o = s.obj(id);
        if (o.owner != dev_seat() || o.card->token) continue;
        const bool knows = buddy_knows(s, id);
        const bool removed = o.zone == Zone::Removed;  // 487.5's unused battlefields: SimDev doesn't track them
        if (knows && dev.truth(id) && !removed && check_truth && o.card != dev.truth(id)) {
            problem = "Dev's " + std::to_string(id) + " is " + o.card->name + ", truly " + dev.truth(id)->name;
        } else if (knows && !known_before_[id] && !announced_.contains(id) && o.card->type != CardType::Battlefield) {
            problem = "Buddy learned Dev's " + o.card->name + " (" + std::to_string(id) + ") unasked";
        }
        known_before_[id] = knows;
    }
    std::erase_if(announced_, [&](ObjId id) { return buddy_knows(s, id); });  // named discards stay until paid
    return problem;
}

std::string SimGame::true_replay_problem() {
    const Replay r = parse_replay(session->replay_text());
    GameState st = new_game(cfg.decks[0], cfg.decks[1], {.seed = cfg.seed});
    const GameState& end = session->state();
    for (ObjId id = 0; id < st.objects.size(); ++id) {  // what Buddy knows at the end: checked = the truth
        if (st.obj(id).owner == dev_seat()) st.obj(id).card = buddy_knows(end, id) ? end.obj(id).card : dev.truth(id);
    }
    try {
        GameChecks checks(st);
        BindMemory memory;
        std::size_t next = 0;
        const auto reveals_until = [&](std::size_t before) {
            for (; next < r.reveals.size() && r.reveals[next].before <= before; ++next) {
                const Reveal& x = r.reveals[next];
                if (x.kind != Reveal::Kind::Order && st.obj(x.obj).owner == dev_seat()) continue;
                apply_reveal(st, x, db, session->buddy(), &memory);
            }
        };
        for (std::size_t i = 0; i < r.actions.size(); ++i) {
            reveals_until(i);
            // A "discard a spell" that the placeholders left without a choice (one spell among them) is a prompt in
            // the session; with Dev's true hand it can be a choice: pick the cards Dev named there.
            const auto legal = legal_actions(st);
            if (std::find(legal.begin(), legal.end(), r.actions[i]) == legal.end() &&
                st.decision.kind == DecisionKind::Choose && st.decision.purpose == Purpose::Discard &&
                st.decision.player == dev_seat() && i > 0) {
                ++collapsed;
                for (const Reveal& x : r.reveals) {
                    if (x.before == i - 1 && x.kind == Reveal::Kind::Bind && st.obj(x.obj).zone == Zone::Hand) {
                        apply(st, {ActionKind::Pick, x.obj});
                    }
                }
            }
            try {
                apply(st, r.actions[i]);
            } catch (const IllegalAction& e) {
                return "action " + std::to_string(i) + ", turn " + std::to_string(st.turn) + ": " + e.what() +
                       "; the true game's decision: " + std::string(to_string(st.decision.kind)) + " " +
                       std::string(to_string(st.decision.purpose)) + " of player " + std::to_string(st.decision.player);
            }
            if (auto p = checks.check(st); !p.empty()) return "action " + std::to_string(i) + ": " + p;
        }
        reveals_until(r.actions.size());
    } catch (const std::exception& e) {
        return e.what();
    }
    if (!(st == dev.true_state(session->state()))) return "the true game ends in another state";
    return {};
}

std::string SimGame::compare() const {
    const GameState& s = session->state();
    const PlayerId b = session->buddy();
    const table::Belief& belief = table.tracker().belief();
    const auto& hand = s.player(b).hand;
    if (hand.size() != belief.hand.size()) {
        return "hand size: engine " + std::to_string(hand.size()) + ", table " + std::to_string(belief.hand.size());
    }
    for (std::size_t i = 0; i < hand.size(); ++i) {
        if (session->table_id(hand[i]) != belief.hand[i].id) return "hand order differs at " + std::to_string(i);
        if (code_of(s, hand[i]) != belief.hand[i].code) {
            return "hand card " + std::to_string(i) + " is " + belief.hand[i].code + " on the table, " +
                   code_of(s, hand[i]) + " in the engine";
        }
    }
    std::vector<int> ids;
    for (const auto& k : belief.hand) ids.push_back(k.id);
    if (session->table_hand() != ids) return "the session's table hand isn't the belief";
    std::map<TPlace, std::vector<std::tuple<int, std::string, bool>>> engine, tab;
    for (ObjId id = 0; id < s.objects.size(); ++id) {
        const Object& o = s.obj(id);
        if (o.owner != b || o.card->type == CardType::Battlefield) continue;
        if (const auto p = spot_of(o)) engine[*p].push_back({session->table_id(id), code_of(s, id), o.exhausted});
    }
    for (int p = 0; p < table::place_count; ++p) {
        for (const auto& k : belief.spots[static_cast<std::size_t>(p)]) {
            tab[static_cast<TPlace>(p)].push_back({k.id, k.code, k.exhausted});
        }
    }
    for (auto* m : {&engine, &tab}) {
        for (auto& [p, list] : *m) std::sort(list.begin(), list.end());
        std::erase_if(*m, [](const auto& kv) { return kv.second.empty(); });
    }
    if (engine != tab) {
        for (const auto& [p, list] : engine) {
            if (tab[p] != list) return std::string(table::to_string(p)) + " differs";
        }
        return "a spot holds cards the engine doesn't";
    }
    if (check_counts) {
        const int held = belief.held ? 1 : 0;  // a peeked card: still the deck's top for the engine
        if (static_cast<int>(s.player(b).main_deck.size()) != belief.deck + held) return "deck count differs";
        if (static_cast<int>(s.player(b).rune_deck.size()) != belief.runes) return "rune deck count differs";
        // the hand's order only as a multiset: a disturbed pile stays unnoticed until a play mismatches (5.5);
        // final_check() rescans and compares the order too
        table::Belief b2 = belief;
        const auto& pile = sim.pile(table.tracker().hand_pile());
        if (pile.size() == b2.hand.size()) {
            std::vector<table::Known> rest = b2.hand, ordered;
            for (const auto& c : pile) {
                const auto it = std::find_if(rest.begin(), rest.end(), [&](const auto& k) { return k.code == c.code; });
                if (it == rest.end()) break;
                ordered.push_back(*it);
                rest.erase(it);
            }
            if (rest.empty()) b2.hand = ordered;
        }
        if (auto d = sim.compare(b2, table.tracker().hand_pile()); !d.empty()) return "belief ≠ truth: " + d;
    }
    return {};
}

std::string SimGame::final_check() {
    table::Command c;
    c.kind = CommandKind::RescanHand;
    c.seq = 1'000'000;
    if (const auto ev = table.execute(c); ev.back().kind != table::EventKind::Done) return "final rescan failed";
    return sim.compare(table.tracker().belief(), table.tracker().hand_pile());
}

std::string SimGame::replay_problem() const {
    const Replay r = parse_replay(session->replay_text());
    const ReplayResult res = run_replay(r, db);
    if (!res.problem.empty()) return res.problem;
    if (!(res.state == session->state())) return "the replay ends in another state";
    return {};
}

std::string SimGame::leak_problem(Rng& rng, int arrangements) {
    const Session& s = *session;
    ++leak_checks;
    const auto want = public_state(s);
    const PlayerId b = s.buddy();
    for (int k = 0; k < arrangements; ++k) {
        GameState r = rearranged(s.state(), dev_seat(), b, dev_fixed(s), rng);
        r = rearranged(r, b, dev_seat(), {}, rng);
        const auto kept = s.kept();
        const auto options = s.status() == Status::DevToAct
                                 ? dev_options(r, {b, kept, &s.named(), &cfg.decks[dev_seat()]})
                                 : std::vector<DevOption>{};
        if (const auto got = public_state(s, r, options); got != want) {
            return "turn " + std::to_string(s.state().turn) + ", " + std::string(to_string(s.status())) +
                   ": the payload changed: " + nlohmann::json::diff(want, got).dump();
        }
    }
    return {};
}

GameState rearranged(const GameState& s, PlayerId owner, PlayerId knower, const std::set<ObjId>& fixed, Rng& rng) {
    GameState out = s;
    for (const bool runes : {false, true}) {
        std::vector<ObjId> ids;
        std::vector<const Card*> cards;
        for (ObjId id = 0; id < s.objects.size(); ++id) {
            const Object& o = s.obj(id);
            if (o.owner != owner || o.known & 1u << knower || fixed.contains(id) || o.card->token) continue;
            if (o.zone != Zone::Hand && o.zone != Zone::MainDeck && o.zone != Zone::RuneDeck) continue;
            if ((o.card->type == CardType::Rune) != runes) continue;
            ids.push_back(id);
            cards.push_back(o.card);
        }
        rng.shuffle(cards);
        for (std::size_t i = 0; i < ids.size(); ++i) out.obj(ids[i]).card = cards[i];
    }
    return out;
}

std::set<ObjId> dev_fixed(const Session& session) {
    std::set<ObjId> fixed(session.named().begin(), session.named().end());
    for (const ObjId id : session.kept()) fixed.insert(id);
    return fixed;
}

} // namespace buddy::game
