// Session games (5.6.2, #48; Dev's real cards #49): Buddy's side through an in-process table_core Table + Sim, Dev a
// SimDev (greedy; the #48 tests with the placeholders as its truth, the SimDev games with a true shuffled deck). After
// every step Buddy's cards as the engine sees them = the table's belief = the sim's truth, Dev's cards Buddy knows =
// SimDev's truth, and every card of Dev's that Buddy learned was prompted or named; at the end the session's replay
// reproduces its state. Fault-free games, each fault alone, every failure path forced through a Backend wrapper, and
// 50 SimDev games.
#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <tuple>

#include "riftbound/ai/greedy.hpp"
#include "riftbound/engine.hpp"
#include "riftbound/hidden.hpp"
#include "riftbound/random_game.hpp"
#include "session.hpp"
#include "sim.hpp"
#include "sim_dev.hpp"
#include "table.hpp"

using namespace buddy;
using namespace buddy::riftbound;
using game::Pause;
using game::Session;
using game::Status;
using table::CommandKind;
using table::Pile;
using TPlace = table::Place;

namespace {

const std::string kDir = std::string(BUDDY_SOURCE_DIR) + "/services/game";

const CardDb& db() {
    static const CardDb d = CardDb::load(kDir + "/tests/data/cards.json");
    return d;
}

std::string deck_text(const std::string& name) {
    std::ifstream in(kDir + "/decks/" + name + ".txt");
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

// Forwards to the sim; for the next n commands every code read on the spot is unreadable, misread as one code, or
// every scan throws (the table reports `failed hardware`).
class Flaky : public table::Backend {
public:
    explicit Flaky(table::Sim& sim) : sim_(sim) {}
    int unreadable = 0, hardware = 0, misread = 0;
    std::string misread_as;

    void begin_command() override {
        sim_.begin_command();
        now_unreadable_ = unreadable > 0 && unreadable-- > 0;
        now_hardware_ = hardware > 0 && hardware-- > 0;
        now_misread_ = misread > 0 && misread-- > 0;
    }
    void setup(int legend, int champion) override { sim_.setup(legend, champion); }
    table::Scan scan(Pile p) override {
        if (now_hardware_) throw std::runtime_error("scanner jammed");
        table::Scan s = sim_.scan(p);
        if (s.kind != table::Scan::Kind::Code || p != Pile::Spot) return s;
        if (now_unreadable_) return {table::Scan::Kind::Unreadable, ""};
        if (now_misread_) s.code = misread_as;
        return s;
    }
    void feed(Pile from, Pile to) override { sim_.feed(from, to); }
    void take(Pile from) override { sim_.take(from); }
    void deal(Pile from) override { sim_.deal(from); }
    void stack(Pile from, Pile onto) override { sim_.stack(from, onto); }
    void place(Pile to, table::End end) override { sim_.place(to, end); }
    void place(TPlace spot, int id) override { sim_.place(spot, id); }
    void put(int id, Pile to, table::End end) override { sim_.put(id, to, end); }
    void put(int id, TPlace spot) override { sim_.put(id, spot); }
    void turn(int id, bool exhausted) override { sim_.turn(id, exhausted); }
    void shuffle_trash_into_deck(const std::vector<int>& order) override { sim_.shuffle_trash_into_deck(order); }
    void token(TPlace spot, int id, const std::string& code) override { sim_.token(spot, id, code); }
    void remove(int id) override { sim_.remove(id); }

private:
    table::Sim& sim_;
    bool now_unreadable_ = false, now_hardware_ = false, now_misread_ = false;
};

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

// What the service will do: the table answers each command at once, the AI answers synchronously, Dev is a SimDev.
struct Game {
    Game(const std::string& d0, const std::string& d1, PlayerId buddy, std::uint64_t seed,
         table::SimFaults faults = {}, std::uint64_t sim_seed = 1, bool true_deck = false)
        : sim({}, faults, sim_seed), flaky(sim), table(flaky, {}), buddy_ai(seed * 2 + 1),
          dev(1 - buddy, std::make_unique<ai::GreedyPlayer>(seed * 2 + 2), seed * 3 + 7, true_deck) {
        cfg.deck_texts = {deck_text(d0), deck_text(d1)};
        cfg.decks = {parse_deck(db(), cfg.deck_texts[0]), parse_deck(db(), cfg.deck_texts[1])};
        cfg.buddy = buddy;
        cfg.seed = seed;
        session = std::make_unique<Session>(db(), cfg);
        buddy_ai.begin(buddy, cfg.decks[0], cfg.decks[1]);
    }

    game::SessionConfig cfg;
    table::Sim sim;
    Flaky flaky;
    table::Table table;
    ai::GreedyPlayer buddy_ai;
    game::SimDev dev;
    std::unique_ptr<Session> session;
    bool check_candidates = false;  // at Dev's priority: the offered cards are exactly the possible ones
    bool check_truth = true;        // Dev's cards Buddy knows = SimDev's truth (off: a test answers for Dev)
    std::set<ObjId> announced;      // Dev's cards prompted or named since the last committed step
    std::vector<std::uint8_t> known_before;  // Dev's objects: Buddy's known bit at the last committed step
    int collapsed = 0;  // true_replay_problem: Dev's choices the placeholders turned into a prompt

    int resumes_left = 20;  // Dev presses resume after a table pause
    bool check_counts = true;
    std::function<void(const table::Command&)> before_cmd;
    std::function<void(const table::Command&, const std::vector<table::Event>&)> after_cmd;
    std::function<std::optional<Action>(const game::AiRequest&)> buddy_choice;
    std::function<bool()> stop_when;  // checked between inputs: true → run() returns
    std::vector<std::string> problems, voice;
    std::vector<Pause> pauses;
    long checked = 0, last_steps = 0;

    PlayerId dev_seat() const { return 1 - session->buddy(); }
    bool buddy_knows(const GameState& s, ObjId id) const { return s.obj(id).known & 1u << session->buddy(); }

    // At Dev's priority: a card is offered iff Dev could still hold it (a copy Buddy hasn't seen) and it's playable
    // from a hidden hand object bound to it.
    std::string candidates_problem() const {
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
            if (!buddy::riftbound::bind(copy, h, e.card, session->buddy())) return "can't bind " + e.card->name;
            if (playable(copy, dev_seat(), h) != is_offered) {
                return e.card->name + (is_offered ? " offered, but not playable" : " playable, but not offered");
            }
        }
        return {};
    }

    void execute(const table::Command& c) {
        if (c.kind == CommandKind::Setup) {
            table::SimSetup s{c.sim_deck, c.sim_runes, c.legend, c.champion, true};
            sim.configure(s);
        }
        if (before_cmd) before_cmd(c);
        const auto events = table.execute(c);
        if (after_cmd) after_cmd(c, events);
        for (const auto& e : events) session->on_table(e);
    }

    // Runs until the game is over, a pause Dev doesn't resume, or stop_when.
    void run() {
        if (session->status() == Status::Setup && session->stats().commands == 0) {
            session->start();
            dev.begin(session->state(), cfg.decks[0], cfg.decks[1]);
            const GameState& s = session->state();
            for (ObjId id = 0; id < s.objects.size(); ++id) known_before.push_back(buddy_knows(s, id));
        }
        for (long guard = 0; guard < 2'000'000; ++guard) {
            if (stop_when && stop_when()) return;  // before take(): what's pending stays in the session
            game::Outputs o = session->take();
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
                if (!a) a = buddy_ai.choose(o.ai->view, o.ai->actions);
                session->on_ai(o.ai->id, *a);
                acted = true;
            }
            if (acted) continue;
            switch (session->status()) {
                case Status::DevToAct: {
                    if (check_candidates) {
                        if (auto p = candidates_problem(); !p.empty()) problems.push_back("candidates: " + p);
                    }
                    const auto o = dev.choose(session->state(), session->dev_options());
                    if (o.named && o.card) announced.insert(o.obj);
                    session->on_dev(o);
                    break;
                }
                case Status::DevToReveal: {
                    const auto& p = *session->prompt();
                    for (const ObjId id : p.objs) {
                        if (buddy_knows(session->state(), id) && p.kind != game::DevPrompt::Kind::Battlefield) {
                            problems.push_back("prompt for a card Buddy knows: " + std::to_string(id));
                        }
                        announced.insert(id);
                    }
                    session->on_reveal(dev.reveal(p));
                    break;
                }
                case Status::Paused:
                    if (session->pause() == Pause::Desync || resumes_left-- <= 0) return;
                    session->resume();
                    break;
                case Status::Over: return;
                default:
                    problems.push_back("stuck in " + std::string(game::to_string(session->status())));
                    return;
            }
        }
        problems.push_back("no end");
    }

    // After every committed step: the engine's view of Buddy's cards = the belief = the truth.
    void check() {
        if (session->stats().steps == last_steps || session->status() == Status::Paused) return;
        last_steps = session->stats().steps;
        ++checked;
        if (auto p = compare(); !p.empty()) {
            problems.push_back("step " + std::to_string(last_steps) + ": " + p);
        }
        if (auto p = dev_problem(); !p.empty()) problems.push_back("step " + std::to_string(last_steps) + ": " + p);
    }

    // Dev's side: every card of Dev's that Buddy knows is SimDev's true one, and Buddy learned it from a prompt or
    // Dev naming it, never on its own; a prompted card is known after its step.
    std::string dev_problem() {
        const GameState& s = session->state();
        known_before.resize(s.objects.size(), 0);
        std::string problem;
        for (ObjId id = 0; id < s.objects.size() && problem.empty(); ++id) {
            const Object& o = s.obj(id);
            if (o.owner != dev_seat() || o.card->token) continue;
            const bool knows = buddy_knows(s, id);
            const bool removed = o.zone == Zone::Removed;  // 487.5's unused battlefields: SimDev doesn't track them
            if (knows && dev.truth(id) && !removed && check_truth && o.card != dev.truth(id)) {
                problem = "Dev's " + std::to_string(id) + " is " + o.card->name + ", truly " + dev.truth(id)->name;
            } else if (knows && !known_before[id] && !announced.contains(id) && o.card->type != CardType::Battlefield) {
                problem = "Buddy learned Dev's " + o.card->name + " (" + std::to_string(id) + ") unasked";
            }
            known_before[id] = knows;
        }
        std::erase_if(announced, [&](ObjId id) { return buddy_knows(s, id); });  // named discards stay until paid
        return problem;
    }

    // The game with Dev's true cards from the start (SimDev's final truth on every object of Dev's, none of Dev's
    // binds) and Buddy's binds replays as a plain legal game (GameChecks) and ends where the session did.
    std::string true_replay_problem() {
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
                    apply_reveal(st, x, db(), session->buddy(), &memory);
                }
            };
            for (std::size_t i = 0; i < r.actions.size(); ++i) {
                reveals_until(i);
                // A "discard a spell" that the placeholders left without a choice (one spell among them) is a
                // prompt in the session; with Dev's true hand it can be a choice: pick the cards Dev named there.
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

    std::string compare() const {
        const GameState& s = session->state();
        const PlayerId b = session->buddy();
        const table::Belief& belief = table.tracker().belief();
        const auto& hand = s.player(b).hand;
        if (hand.size() != belief.hand.size()) return "hand size: engine " + std::to_string(hand.size()) + ", table " + std::to_string(belief.hand.size());
        for (std::size_t i = 0; i < hand.size(); ++i) {
            if (session->table_id(hand[i]) != belief.hand[i].id) return "hand order differs at " + std::to_string(i);
            if (game::code_of(s, hand[i]) != belief.hand[i].code) return "hand card " + std::to_string(i) + " is " + belief.hand[i].code + " on the table, " + game::code_of(s, hand[i]) + " in the engine";
        }
        if (session->table_hand() != [&] { std::vector<int> v; for (const auto& k : belief.hand) v.push_back(k.id); return v; }()) {
            return "the session's table hand isn't the belief";
        }
        std::map<TPlace, std::vector<std::tuple<int, std::string, bool>>> engine, tab;
        for (ObjId id = 0; id < s.objects.size(); ++id) {
            const Object& o = s.obj(id);
            if (o.owner != b || o.card->type == CardType::Battlefield) continue;
            if (const auto p = spot_of(o)) engine[*p].push_back({session->table_id(id), game::code_of(s, id), o.exhausted});
        }
        for (int p = 0; p < table::place_count; ++p) {
            for (const auto& k : belief.spots[static_cast<std::size_t>(p)]) tab[static_cast<TPlace>(p)].push_back({k.id, k.code, k.exhausted});
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

    // At the end: a rescan of the hand, then the belief is the truth, order included.
    std::string final_check() {
        table::Command c;
        c.kind = CommandKind::RescanHand;
        c.seq = 1'000'000;
        if (const auto ev = table.execute(c); ev.back().kind != table::EventKind::Done) return "final rescan failed";
        return sim.compare(table.tracker().belief(), table.tracker().hand_pile());
    }

    // The session's replay reproduces its state.
    std::string replay_problem() const {
        const Replay r = parse_replay(session->replay_text());
        const ReplayResult res = run_replay(r, db());
        if (!res.problem.empty()) return res.problem;
        if (!(res.state == session->state())) return "the replay ends in another state";
        return {};
    }
};

const std::array<std::pair<const char*, const char*>, 2> kDecks{{{"vi-unleashed", "zed-vendetta"}, {"zed-vendetta", "vi-unleashed"}}};

bool has_voice(const Game& g, const std::string& part) {
    return std::any_of(g.voice.begin(), g.voice.end(), [&](const auto& v) { return v.find(part) != std::string::npos; });
}

// The cheapest unit in Buddy's deck other than `not_code`, as the code a misread reports.
std::string cheap_unit(const GameState& s, PlayerId p, const std::string& not_code) {
    std::string best;
    int cost = 99;
    for (const ObjId id : s.player(p).main_deck) {
        const Card& c = *s.obj(id).card;
        const std::string code = game::code_of(s, id);
        if (c.type != CardType::Unit || code == not_code || !c.energy || *c.energy >= cost) continue;
        cost = *c.energy;
        best = code;
    }
    return best;
}

} // namespace

TEST(RiftboundSession, FaultFreeGamesPlayToTheEnd) {
    for (const auto& [d0, d1] : kDecks) {
        for (PlayerId buddy = 0; buddy < 2; ++buddy) {
            Game g(d0, d1, buddy, 100 + buddy);
            g.run();
            SCOPED_TRACE(std::string(d0) + " vs " + d1 + ", Buddy seat " + std::to_string(buddy));
            EXPECT_EQ(g.session->status(), Status::Over) << g.session->pause_message();
            EXPECT_TRUE(g.problems.empty()) << g.problems.front();
            EXPECT_TRUE(g.pauses.empty());
            EXPECT_GT(g.checked, 50);
            const auto& st = g.session->stats();
            EXPECT_GT(st.binds, 10);
            EXPECT_GT(st.reapplies, 0);  // the sim's deck order isn't the engine's
            EXPECT_EQ(st.re_decides + st.retries + st.corrections + st.extras, 0);
            EXPECT_EQ(g.replay_problem(), "");
            EXPECT_EQ(g.final_check(), "");
            EXPECT_TRUE(has_voice(g, "Let's play"));
        }
    }
}

// Each sim fault alone at five times the stress rate (at least 10 %): no desync, every game ends, the checks hold after every step.
TEST(RiftboundSession, EachFaultAlone) {
    const table::SimFaults stress = table::parse_faults("stress");
    const std::vector<std::pair<std::string, double table::SimFaults::*>> faults{
        {"misread", &table::SimFaults::misread},         {"unreadable", &table::SimFaults::unreadable},
        {"double_feed", &table::SimFaults::double_feed}, {"missed_feed", &table::SimFaults::missed_feed},
        {"double_take", &table::SimFaults::double_take}, {"double_deal", &table::SimFaults::double_deal},
        {"disturbed", &table::SimFaults::disturbed}};
    for (const auto& [name, field] : faults) {
        game::SessionStats sum;
        long injected = 0;
        for (int i = 0; i < 2; ++i) {
            table::SimFaults f;
            f.*field = std::max(0.1, 5 * (stress.*field));
            const auto& [d0, d1] = kDecks[static_cast<std::size_t>(i)];
            Game g(d0, d1, static_cast<PlayerId>(i), 200 + static_cast<std::uint64_t>(i), f, 7 + static_cast<std::uint64_t>(i));
            g.run();
            SCOPED_TRACE(name + " game " + std::to_string(i));
            EXPECT_EQ(g.session->status(), Status::Over) << g.session->pause_message();
            EXPECT_TRUE(g.problems.empty()) << g.problems.front();
            EXPECT_EQ(std::count(g.pauses.begin(), g.pauses.end(), Pause::Desync), 0);
            EXPECT_EQ(g.replay_problem(), "");
            EXPECT_EQ(g.final_check(), "");
            const auto& st = g.session->stats();
            sum.retries += st.retries;
            sum.corrections += st.corrections;
            sum.reorders += st.reorders;
            sum.pauses += st.pauses;
            injected += g.sim.faults().total();
        }
        EXPECT_GT(injected, 0) << name;
        // unreadable alone rarely fails a deal (reads vote over up to 10 scans): UnreadableDealIsRetriedOnce forces it
        if (name == "disturbed") {
            EXPECT_GT(sum.reorders, 0);
        }
        std::cout << name << ": injected " << injected << ", retries " << sum.retries << ", corrections "
                  << sum.corrections << ", reorders " << sum.reorders << ", pauses " << sum.pauses << "\n";
    }
}

// A misread at draw that the votes accepted: Buddy plays the card, the table finds another one there (not_in_hand
// after a recovery that replaced it), the session rebinds it, says so, and the AI decides again.
TEST(RiftboundSession, MisreadAtDrawIsReboundAndBuddyDecidesAgain) {
    Game g("vi-unleashed", "zed-vendetta", 0, 300);
    const PlayerId b = 0;
    int deals = 0;
    int misread_id = 0;
    std::string truth;
    g.before_cmd = [&](const table::Command& c) {
        if (c.kind != CommandKind::Deal || c.to != TPlace::Hand || ++deals != 5) return;  // the last opening card
        truth = g.sim.pile(Pile::Deck).back().code;
        g.flaky.misread_as = cheap_unit(g.session->state(), b, truth);
        g.flaky.misread = 1;
    };
    g.after_cmd = [&](const table::Command& c, const std::vector<table::Event>& events) {
        if (c.kind == CommandKind::Deal && deals == 5 && misread_id == 0) misread_id = events.back().cards.at(0).id;
    };
    g.buddy_choice = [&](const game::AiRequest& r) -> std::optional<Action> {
        const ObjId obj = g.session->obj_of(misread_id);
        for (const Action& a : r.actions) {
            if (a.kind == ActionKind::Play && a.obj == obj) return a;
        }
        return std::nullopt;
    };
    g.check_counts = false;  // the hand is wrong on purpose until Buddy plays the card
    g.stop_when = [&] { return g.session->stats().re_decides > 0; };
    g.run();
    ASSERT_EQ(g.session->stats().re_decides, 1) << g.session->pause_message();
    ASSERT_FALSE(g.flaky.misread_as.empty());
    EXPECT_EQ(g.session->stats().misreads, 1);
    const ObjId obj = g.session->obj_of(misread_id);
    ASSERT_NE(obj, no_obj);
    EXPECT_EQ(game::code_of(g.session->state(), obj), truth);
    g.stop_when = nullptr;
    g.buddy_choice = nullptr;
    g.check_counts = true;
    g.run();
    EXPECT_EQ(g.session->status(), Status::Over) << g.session->pause_message();
    EXPECT_TRUE(g.problems.empty()) << g.problems.front();
    EXPECT_TRUE(has_voice(g, "misread"));
    EXPECT_EQ(g.replay_problem(), "");
}

TEST(RiftboundSession, UnreadableDealIsRetriedOnce) {
    Game g("zed-vendetta", "vi-unleashed", 1, 310);
    int deals = 0;
    g.before_cmd = [&](const table::Command& c) {
        if (c.kind == CommandKind::Deal && ++deals == 8) g.flaky.unreadable = 1;
    };
    g.run();
    EXPECT_EQ(g.session->status(), Status::Over);
    EXPECT_TRUE(g.problems.empty()) << g.problems.front();
    EXPECT_EQ(g.session->stats().retries, 1);
    EXPECT_TRUE(g.pauses.empty());
    EXPECT_EQ(g.replay_problem(), "");
}

TEST(RiftboundSession, UnreadableTwicePausesUntilResume) {
    Game g("vi-unleashed", "zed-vendetta", 1, 320);
    int deals = 0;
    long steps_at_pause = -1;
    g.before_cmd = [&](const table::Command& c) {
        if (c.kind == CommandKind::Deal && ++deals == 8) g.flaky.unreadable = 2;
    };
    g.resumes_left = 0;
    g.run();
    ASSERT_EQ(g.session->status(), Status::Paused);
    EXPECT_EQ(g.session->pause(), Pause::Unreadable);
    EXPECT_TRUE(has_voice(g, "can't read"));
    steps_at_pause = g.session->stats().steps;
    const GameState before = g.session->state();
    EXPECT_THROW(g.session->on_dev(game::DevOption{}), std::logic_error);
    g.resumes_left = 1;
    g.run();
    EXPECT_EQ(g.session->status(), Status::Over);
    EXPECT_GT(g.session->stats().steps, steps_at_pause);
    EXPECT_EQ(g.session->stats().resumes, 1);
    EXPECT_TRUE(g.problems.empty()) << g.problems.front();
    EXPECT_EQ(g.replay_problem(), "");
}

// The table fails mid-step: the state doesn't commit; after resume (rescan) the step goes on where it stopped.
TEST(RiftboundSession, HardwareFailurePausesMidStep) {
    Game g("zed-vendetta", "vi-unleashed", 0, 330);
    int plays = 0;
    g.before_cmd = [&](const table::Command& c) {
        if (c.kind == CommandKind::Play && ++plays == 3) g.flaky.hardware = 1;
    };
    g.resumes_left = 0;
    g.run();
    ASSERT_EQ(g.session->status(), Status::Paused);
    EXPECT_EQ(g.session->pause(), Pause::Hardware);
    const long steps = g.session->stats().steps;
    g.resumes_left = 1;
    g.run();
    EXPECT_EQ(g.session->status(), Status::Over);
    EXPECT_GT(g.session->stats().steps, steps);
    EXPECT_TRUE(g.problems.empty()) << g.problems.front();
    EXPECT_EQ(g.replay_problem(), "");
}

// A hand card disappears: the next play recovers and reports it missing, the session pauses; once Dev puts it back
// the rescan finds it as an extra card, which is the lost one: same object, the game goes on.
TEST(RiftboundSession, MissingCardPausesUntilItIsBack) {
    Game g("vi-unleashed", "zed-vendetta", 0, 340);
    std::string lost;
    g.stop_when = [&] {
        return g.session->status() == Status::BuddyThinking && g.table.tracker().belief().hand.size() >= 3 &&
               g.session->stats().steps > 10;
    };
    g.run();
    ASSERT_EQ(g.session->status(), Status::BuddyThinking);
    const Pile h = g.table.tracker().hand_pile();
    lost = g.sim.pile(h).front().code;
    g.sim.take(h);  // the bottom card off the hand, gone
    g.sim.place(TPlace::Banish, 9999);
    g.sim.remove(9999);
    g.stop_when = nullptr;
    g.resumes_left = 0;
    g.check_counts = false;
    g.run();
    ASSERT_EQ(g.session->status(), Status::Paused) << (g.problems.empty() ? "" : g.problems.front());
    EXPECT_EQ(g.session->pause(), Pause::Missing);
    // resumed without the card: still missing
    g.resumes_left = 1;
    g.run();
    ASSERT_EQ(g.session->status(), Status::Paused);
    EXPECT_EQ(g.session->pause(), Pause::Missing);
    g.sim.token(TPlace::Base, 9998, lost);  // Dev found it: back on the hand pile
    g.sim.put(9998, g.table.tracker().hand_pile(), table::End::Top);
    g.check_counts = true;
    g.resumes_left = 1;
    g.run();
    EXPECT_EQ(g.session->status(), Status::Over) << g.session->pause_message();
    EXPECT_TRUE(g.problems.empty()) << g.problems.front();
    EXPECT_EQ(g.replay_problem(), "");
}

// A card nobody knew of in the hand (an undetected double deal): back on the deck top.
TEST(RiftboundSession, ExtraCardGoesBackOnTheDeckTop) {
    Game g("zed-vendetta", "vi-unleashed", 1, 350);
    g.stop_when = [&] { return g.session->status() == Status::BuddyThinking && g.session->stats().steps > 10; };
    g.run();
    ASSERT_EQ(g.session->status(), Status::BuddyThinking);
    g.sim.deal(Pile::Deck);
    g.sim.place(g.table.tracker().hand_pile(), table::End::Bottom);
    g.stop_when = [&] { return g.session->stats().extras > 0 && !g.session->state().winner; };
    g.check_counts = false;  // the belief counts the dealt card twice (deck and extra): the tracker's business
    g.run();
    EXPECT_EQ(g.session->stats().extras, 1);
    g.stop_when = nullptr;
    g.run();
    EXPECT_EQ(g.session->status(), Status::Over) << g.session->pause_message();
    EXPECT_TRUE(g.problems.empty()) << g.problems.front();
}

// Hand piles disturbed until a recovery notices: the engine adopts the table's order (an order line in the replay).
TEST(RiftboundSession, ChangedHandOrderIsAdopted) {
    Game g("vi-unleashed", "zed-vendetta", 1, 360);
    g.sim.rates().disturbed = 0.3;
    g.stop_when = [&] { return g.session->stats().reorders > 0; };
    g.run();
    ASSERT_GT(g.session->stats().reorders, 0) << g.session->pause_message();
    g.sim.rates().disturbed = 0;
    g.stop_when = nullptr;
    g.run();
    EXPECT_EQ(g.session->status(), Status::Over);
    EXPECT_TRUE(g.problems.empty()) << g.problems.front();
    EXPECT_NE(g.session->replay_text().find("\norder 1 "), std::string::npos);
    EXPECT_EQ(g.replay_problem(), "");
}

// A re-applied step whose commands no longer start with the ones already run: desync, paused for good, replay noted.
TEST(RiftboundSession, DesyncIsDetected) {
    Game g("zed-vendetta", "vi-unleashed", 0, 370);
    long seen = 0;  // re-applies count up only for re-applications, not a step's first computation
    bool done = false;
    g.session->on_moves = [&](std::vector<game::TableMove>& list) {
        const long now = g.session->stats().reapplies;
        const bool reapplied = now > seen;
        seen = now;
        if (done || !reapplied || g.session->stats().steps < 5 || list.empty()) return;
        list[0].cmd.to = list[0].cmd.to == TPlace::Trash ? TPlace::Base : TPlace::Trash;
        done = true;
    };
    g.run();
    ASSERT_EQ(g.session->status(), Status::Paused);
    EXPECT_EQ(g.session->pause(), Pause::Desync);
    EXPECT_NE(g.session->replay_text().find("# desync: command 1 was "), std::string::npos);
    EXPECT_THROW(g.session->resume(), std::logic_error);
    g.session->concede(1);
    EXPECT_EQ(g.session->status(), Status::Over);
}

TEST(RiftboundSession, DevActionsAreChecked) {
    Game g("vi-unleashed", "zed-vendetta", 1, 380);
    g.stop_when = [&] { return g.session->status() == Status::DevToAct; };
    g.run();
    ASSERT_EQ(g.session->status(), Status::DevToAct);
    const GameState before = g.session->state();
    EXPECT_THROW(g.session->on_dev({.action = {.kind = ActionKind::Play, .obj = 9999}}), game::Refused);
    EXPECT_TRUE(g.session->state() == before);
    EXPECT_EQ(g.session->take().table.size(), 0u);
    g.session->concede(0);
    EXPECT_EQ(g.session->status(), Status::Over);
    EXPECT_EQ(g.session->state().winner, std::optional<PlayerId>(1));
}

// #49: Dev names their battlefield first (before both mulligans); impossible answers are refused and change nothing.
TEST(RiftboundSession, DevNamesTheBattlefieldFirst) {
    Game g("vi-unleashed", "zed-vendetta", 0, 390);
    g.stop_when = [&] { return g.session->status() == Status::DevToReveal; };
    g.run();
    ASSERT_EQ(g.session->status(), Status::DevToReveal);
    const game::DevPrompt p = *g.session->prompt();
    ASSERT_EQ(p.kind, game::DevPrompt::Kind::Battlefield);
    ASSERT_EQ(p.objs.size(), 1u);
    EXPECT_EQ(g.session->state().decision.kind, DecisionKind::Mulligan);
    EXPECT_EQ(g.session->stats().steps, 0);
    const GameState& s = g.session->state();
    const ObjId field = p.objs[0];
    g.check_truth = false;  // the test names another battlefield than SimDev's
    const Card* other = nullptr;  // one of Dev's battlefields the engine didn't pick
    for (const Object& o : s.objects) {
        if (o.owner == g.dev_seat() && o.zone == Zone::Removed && o.card->type == CardType::Battlefield) other = o.card;
    }
    ASSERT_NE(other, nullptr);
    const GameState before = s;
    const std::string replay = g.session->replay_text();
    const Card* buddys = s.obj(s.battlefields[0].card).card;
    const Card* rune = s.obj(s.player(g.dev_seat()).rune_deck.back()).card;
    for (const auto& wrong : std::vector<std::vector<const Card*>>{{}, {other, other}, {buddys}, {rune}}) {
        EXPECT_THROW(g.session->on_reveal(wrong), game::Refused);
        EXPECT_TRUE(g.session->state() == before);
        EXPECT_EQ(g.session->replay_text(), replay);
        EXPECT_EQ(g.session->status(), Status::DevToReveal);
    }
    EXPECT_EQ(g.session->stats().refusals, 4);
    g.session->on_reveal({other});
    EXPECT_EQ(g.session->state().obj(field).card, other);
    EXPECT_NE(g.session->replay_text().find("bind " + std::to_string(field) + " " + other->home().code.str()),
              std::string::npos);
    g.stop_when = nullptr;
    g.run();
    EXPECT_EQ(g.session->status(), Status::Over) << g.session->pause_message();
    EXPECT_TRUE(g.problems.empty()) << g.problems.front();
    EXPECT_EQ(g.replay_problem(), "");
}

// #49: channelled runes and burned cards are prompts; a card Dev can't have left or the wrong kind is refused.
TEST(RiftboundSession, DevPromptsRefuseImpossibleCards) {
    Game g("zed-vendetta", "vi-unleashed", 1, 391);
    std::map<game::DevPrompt::Kind, int> seen;
    bool tried_runes = false;
    g.stop_when = [&] {
        if (g.session->status() != Status::DevToReveal) return false;
        const auto& p = *g.session->prompt();
        ++seen[p.kind];
        if (p.kind != game::DevPrompt::Kind::Runes || tried_runes) return false;
        tried_runes = true;
        return true;
    };
    g.run();
    ASSERT_EQ(g.session->status(), Status::DevToReveal);
    const GameState before = g.session->state();
    const game::DevPrompt p = *g.session->prompt();
    for (const ObjId id : p.objs) {
        EXPECT_EQ(before.obj(id).zone, Zone::RuneDeck);
        EXPECT_FALSE(before.obj(id).known & 1u << g.session->buddy());
    }
    const Card* legend = before.obj(before.player(g.dev_seat()).legend.front()).card;
    std::vector<const Card*> wrong(p.objs.size(), legend);
    EXPECT_THROW(g.session->on_reveal(wrong), game::Refused);
    EXPECT_TRUE(g.session->state() == before);
    g.stop_when = nullptr;
    g.run();
    EXPECT_EQ(g.session->status(), Status::Over) << g.session->pause_message();
    EXPECT_TRUE(g.problems.empty()) << g.problems.front();
    EXPECT_EQ(g.replay_problem(), "");
    EXPECT_GT(g.session->stats().prompts, 3);
}

// #49's gate: SimDev (a true shuffled deck, greedy) against Buddy (greedy), both decks from both seats, fault-free
// table. No refusal, no desync, no pause; Dev's options always hold SimDev's true move, the offered cards are exactly
// the possible ones, every card Buddy learns of Dev's comes from a prompt or Dev's choice and is SimDev's true card,
// and the game with Dev's true cards from the start is the same legal game. 50 games in 5 tests (ctest runs them in
// parallel).
void sim_dev_games(int first) {
    game::SessionStats sum;
    int collapsed = 0;
    for (int i = first; i < first + 10; ++i) {
        const auto& [d0, d1] = kDecks[static_cast<std::size_t>(i % 2)];
        const auto buddy = static_cast<PlayerId>(i / 2 % 2);
        Game g(d0, d1, buddy, 1000 + static_cast<std::uint64_t>(i), {}, 11 + static_cast<std::uint64_t>(i), true);
        g.check_candidates = true;
        g.run();
        SCOPED_TRACE("game " + std::to_string(i) + ": " + d0 + " vs " + d1 + ", Buddy seat " + std::to_string(buddy));
        EXPECT_EQ(g.session->status(), Status::Over) << g.session->pause_message();
        EXPECT_TRUE(g.problems.empty()) << g.problems.front();
        EXPECT_TRUE(g.pauses.empty());
        const auto& st = g.session->stats();
        EXPECT_EQ(st.refusals, 0);
        EXPECT_EQ(g.replay_problem(), "");
        EXPECT_EQ(g.true_replay_problem(), "");
        EXPECT_EQ(g.final_check(), "");
        sum.dev_decisions += st.dev_decisions;
        sum.dev_forced += st.dev_forced;
        sum.dev_named += st.dev_named;
        sum.dev_assumed += st.dev_assumed;
        sum.prompts += st.prompts;
        collapsed += g.collapsed;
    }
    EXPECT_GT(sum.dev_named, 20);
    EXPECT_GT(sum.prompts, 20);
    std::cout << "SimDev games " << first << "–" << first + 9 << ": Dev decisions " << sum.dev_decisions << ", forced "
              << sum.dev_forced << ", named " << sum.dev_named << ", assumed " << sum.dev_assumed << ", prompts " << sum.prompts << ", choices collapsed into a prompt " << collapsed << "\n";
}

TEST(RiftboundSession, SimDevGames0) { sim_dev_games(0); }
TEST(RiftboundSession, SimDevGames1) { sim_dev_games(10); }
TEST(RiftboundSession, SimDevGames2) { sim_dev_games(20); }
TEST(RiftboundSession, SimDevGames3) { sim_dev_games(30); }
TEST(RiftboundSession, SimDevGames4) { sim_dev_games(40); }
