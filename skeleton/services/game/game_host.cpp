#include "game_host.hpp"

#include <algorithm>
#include <random>
#include <set>

#include "public_state.hpp"
#include "riftbound/ai/greedy.hpp"
#include "riftbound/ai/player.hpp"

namespace buddy::game {

using nlohmann::json;

namespace {

// Thrown while parsing a command: the refusal's message.
struct Bad {
    std::string why;
};

void only(const json& cmd, std::initializer_list<const char*> fields) {
    for (const auto& [key, _] : cmd.items()) {
        if (key == "action") continue;
        if (std::none_of(fields.begin(), fields.end(), [&](const char* f) { return key == f; })) {
            throw Bad{"unknown field " + key};
        }
    }
}

std::string text(const json& cmd, const char* field) {
    const auto it = cmd.find(field);
    if (it == cmd.end()) throw Bad{std::string("missing ") + field};
    if (!it->is_string()) throw Bad{std::string(field) + " must be a string"};
    return it->get<std::string>();
}

std::uint64_t number(const json& cmd, const char* field) {
    const auto it = cmd.find(field);
    if (it == cmd.end()) throw Bad{std::string("missing ") + field};
    if (!it->is_number_integer() || it->get<std::int64_t>() < 0) throw Bad{std::string(field) + " must be a number ≥ 0"};
    return it->get<std::uint64_t>();
}

} // namespace

GameHost::GameHost(const riftbound::CardDb& db, HostConfig cfg) : db_(db), cfg_(std::move(cfg)) {
    if (!cfg_.random_seed) cfg_.random_seed = [] { return (std::uint64_t{std::random_device{}()} << 32) | std::random_device{}(); };
    state_ = {{"v", v_}, {"status", "idle"}};
}

Reply GameHost::command(const json& cmd) {
    try {
        if (!cmd.is_object()) throw Bad{"a command is a JSON object"};
        const std::string action = text(cmd, "action");
        if (action == "new") return start(cmd);
        if (action != "act" && action != "reveal" && action != "resume" && action != "concede") {
            throw Bad{"unknown action " + action};
        }
        if (!session_) throw Bad{"no game: start one with new"};
        Session& s = *session_;
        if (action == "concede") {
            only(cmd, {});
            if (s.status() == Status::Over) throw Bad{"the game is over"};
            s.concede(riftbound::opponent(s.buddy()));
        } else if (action == "resume") {
            only(cmd, {});
            if (s.status() != Status::Paused || s.pause() == Pause::Desync) throw Bad{"nothing to resume"};
            s.resume();
        } else {
            only(cmd, {"v", action == "act" ? "i" : "cards"});
            if (static_cast<std::int64_t>(number(cmd, "v")) != v_) {
                throw Bad{"the game moved on: v " + std::to_string(v_) + ", not " + std::to_string(number(cmd, "v"))};
            }
            if (action == "act") {
                if (s.status() != Status::DevToAct) throw Bad{"not your decision"};
                const auto i = number(cmd, "i");
                if (i >= s.dev_options().size()) throw Bad{"no option " + std::to_string(i)};
                s.on_dev(s.dev_options()[i]);
            } else {
                if (s.status() != Status::DevToReveal) throw Bad{"no card to name"};
                const auto it = cmd.find("cards");
                if (it == cmd.end() || !it->is_array()) throw Bad{"cards must be a list of codes"};
                std::vector<const riftbound::Card*> cards;
                for (const auto& c : *it) {
                    if (!c.is_string()) throw Bad{"cards must be a list of codes"};
                    const auto found = db_.find_code(c.get<std::string>());
                    if (!found.card) throw Bad{c.get<std::string>() + ": " + found.error};
                    cards.push_back(found.card);
                }
                s.on_reveal(cards);
            }
        }
    } catch (const Bad& b) {
        return {false, b.why};
    } catch (const Refused& r) {
        return {false, r.what()};
    }
    absorb();
    drive();
    return {};
}

Reply GameHost::start(const json& cmd) {
    only(cmd, {"buddy", "seed", "dev"});
    if (session_ && session_->status() != Status::Over) throw Bad{"a game is running: concede first"};
    const std::string buddy = text(cmd, "buddy");
    const auto mine = cfg_.decks.find(buddy);
    if (mine == cfg_.decks.end()) throw Bad{"unknown deck " + buddy};
    const auto theirs = std::find_if(cfg_.decks.begin(), cfg_.decks.end(), [&](const auto& d) { return d.first != buddy; });
    if (theirs == cfg_.decks.end()) throw Bad{"no deck for you"};
    const std::string dev = text(cmd, "dev");
    if (dev != "human" && dev != "greedy" && dev != "random") throw Bad{"dev must be human, greedy or random"};
    const std::uint64_t seed = cmd.contains("seed") ? number(cmd, "seed") : cfg_.random_seed();

    // Buddy sits in seat 0: the first player is the engine's coin flip (seed), seats mean nothing else.
    SessionConfig sc;
    sc.decks = {mine->second.deck, theirs->second.deck};
    sc.deck_texts = {mine->second.text, theirs->second.text};
    sc.buddy = 0;
    sc.seed = seed;
    out_ = {};  // the old game's unsent outputs are moot
    out_.new_game = true;
    session_ = std::make_unique<Session>(db_, sc);
    bot_.reset();
    if (dev != "human") {
        std::unique_ptr<riftbound::ai::Player> p;
        if (dev == "greedy") p = std::make_unique<riftbound::ai::GreedyPlayer>(seed * 2 + 1);
        else p = std::make_unique<riftbound::ai::RandomPlayer>(seed * 2 + 1);
        bot_ = std::make_unique<SimDev>(1, std::move(p), seed * 3 + 7);
    }
    catalogue_ = catalogue(sc.decks[0], sc.decks[1]);
    session_->start();
    if (bot_) bot_->begin(session_->state(), sc.decks[0], sc.decks[1]);
    absorb();
    drive();
    return {};
}

void GameHost::on_table(const table::Event& e) {
    if (!session_) return;
    session_->on_table(e);
    absorb();
    drive();
}

void GameHost::on_ai(std::uint64_t id, const riftbound::Action& a) {
    if (!session_) return;
    session_->on_ai(id, a);
    absorb();
    drive();
}

void GameHost::absorb() {
    Outputs o = session_->take();
    for (auto& c : o.table) out_.table.push_back(std::move(c));
    if (o.ai) out_.ai = std::move(o.ai);
    for (auto& l : o.voice) out_.voice.push_back(std::move(l));
    for (auto& l : o.replay) out_.replay.push_back(std::move(l));
}

void GameHost::drive() {
    while (bot_) {
        Session& s = *session_;
        if (s.status() == Status::DevToAct) s.on_dev(bot_->choose(s.state(), s.dev_options()));
        else if (s.status() == Status::DevToReveal) s.on_reveal(bot_->reveal(*s.prompt()));
        else return;
        absorb();
    }
}

HostOutputs GameHost::take() {
    if (session_) {
        json game = public_state(*session_);
        if (game != game_ || out_.new_game) {
            game_ = std::move(game);
            state_ = game_;
            state_["v"] = ++v_;
            state_["catalogue"] = catalogue_;
            out_.state = state_;
        }
    }
    return std::exchange(out_, {});
}

} // namespace buddy::game
