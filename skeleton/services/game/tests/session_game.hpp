#pragma once
// The session test harness (#48–#51): game::SimGame (a Session with Buddy's side on an in-process table_core Table +
// Sim, Dev a SimDev, the checks after every step) with a Flaky backend that forces table failures. Shared by
// test_session.cpp, test_public_state.cpp, test_game_host.cpp and test_sim_soak.cpp.
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
#include "sim_game.hpp"
#include "table.hpp"

using namespace buddy;
using namespace buddy::riftbound;
using game::Pause;
using game::Session;
using game::Status;
using table::CommandKind;
using table::Pile;
using TPlace = table::Place;

namespace session_test {

inline const std::string kDir = std::string(BUDDY_SOURCE_DIR) + "/services/game";

inline const CardDb& db() {
    static const CardDb d = CardDb::load(kDir + "/tests/data/cards.json");
    return d;
}

inline std::string deck_text(const std::string& name) {
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

// The service's way of running a game with the checks after every step (game_sim, #51), the table behind a Flaky.
struct Game : game::SimGame {
    Game(const std::string& d0, const std::string& d1, PlayerId buddy, std::uint64_t seed,
         table::SimFaults faults = {}, std::uint64_t sim_seed = 1, bool true_deck = false)
        : SimGame(session_test::db(), config(d0, d1, buddy, seed, faults, sim_seed, true_deck)),
          flaky(static_cast<Flaky&>(*wrapper)) {}

    Flaky& flaky;

private:
    static game::SimGameConfig config(const std::string& d0, const std::string& d1, PlayerId buddy, std::uint64_t seed,
                                      table::SimFaults faults, std::uint64_t sim_seed, bool true_deck) {
        game::SimGameConfig c;
        c.deck_texts = {deck_text(d0), deck_text(d1)};
        c.decks = {parse_deck(session_test::db(), c.deck_texts[0]), parse_deck(session_test::db(), c.deck_texts[1])};
        c.buddy = buddy;
        c.seed = seed;
        c.faults = faults;
        c.sim_seed = sim_seed;
        c.true_deck = true_deck;
        c.wrap = [](table::Sim& sim) { return std::make_unique<Flaky>(sim); };
        return c;
    }
};

using game::dev_fixed;
using game::rearranged;

inline const std::array<std::pair<const char*, const char*>, 2> kDecks{{{"vi-unleashed", "zed-vendetta"}, {"zed-vendetta", "vi-unleashed"}}};

inline bool has_voice(const Game& g, const std::string& part) {
    return std::any_of(g.voice.begin(), g.voice.end(), [&](const auto& v) { return v.find(part) != std::string::npos; });
}

// The cheapest unit in Buddy's deck other than `not_code`, as the code a misread reports.
inline std::string cheap_unit(const GameState& s, PlayerId p, const std::string& not_code) {
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

} // namespace session_test

using namespace session_test;
