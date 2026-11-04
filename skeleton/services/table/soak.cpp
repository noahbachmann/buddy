#include "soak.hpp"

#include <algorithm>
#include <iomanip>
#include <istream>
#include <ostream>

#include <nlohmann/json.hpp>

namespace buddy::table {

using json = nlohmann::json;

namespace {

bool into_a_deck(Place p) { return p == Place::DeckTop || p == Place::DeckBottom || p == Place::RunesBottom; }

double percentile(std::vector<int> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const auto rank = static_cast<std::size_t>(std::max(0.0, p / 100.0 * static_cast<double>(v.size()) - 1e-9));
    return v[std::min(rank, v.size() - 1)];
}

} // namespace

StandIn::StandIn(const json& setup, const SoakOptions& options, std::uint64_t game) {
    SimSetup s;
    s.deck = setup.at("deck").get<std::vector<std::string>>();
    s.runes = setup.at("runes").get<std::vector<std::string>>();
    s.legend = setup.at("legend").get<std::string>();
    s.champion = setup.at("champion").get<std::string>();
    sim_ = std::make_unique<Sim>(s, options.faults, options.seed * 1000003 + game);
    table_ = std::make_unique<Table>(*sim_, options.tracker);
    const json& objs = setup.at("objs");
    const auto deck = objs.at("deck").get<std::vector<int>>();
    const auto runes = objs.at("runes").get<std::vector<int>>();
    for (std::size_t i = 0; i < deck.size(); ++i) code_[deck[i]] = s.deck.at(i);
    for (std::size_t i = 0; i < runes.size(); ++i) code_[runes[i]] = s.runes.at(i);
    const int legend = objs.at("legend").get<int>(), champion = objs.at("champion").get<int>();
    code_[legend] = s.legend;
    code_[champion] = s.champion;
    const auto events = run({.kind = CommandKind::Setup, .legend = s.legend, .champion = s.champion,
                             .deck = static_cast<int>(s.deck.size()), .runes = static_cast<int>(s.runes.size())});
    if (events.back().kind != EventKind::Done) {
        unrecovered("setup failed");
        return;
    }
    id_[legend] = events.back().cards.at(0).id;
    id_[champion] = events.back().cards.at(1).id;
}

int StandIn::id_of(int obj) const {
    const auto it = id_.find(obj);
    return it == id_.end() ? -1 : it->second;
}

void StandIn::unrecovered(const std::string& what) { stats_.unrecovered.push_back(what); }

std::vector<Event> StandIn::run(Command c) {
    c.seq = ++seq_;
    ++stats_.commands;
    auto events = table_->execute(c);
    for (const Event& e : events) {
        if (e.kind != EventKind::HandCorrected) continue;
        ++stats_.corrections;
        stats_.replaced += static_cast<long>(e.replaced.size());
        stats_.missing += static_cast<long>(e.missing.size());
        for (const CardRef& x : e.extra) {  // a card the engine doesn't know: back on the deck top
            ++stats_.extra;
            const auto back = table_->execute({.kind = CommandKind::Play, .seq = ++seq_, .to = Place::DeckTop, .id = x.id});
            if (back.back().kind != EventKind::Done) unrecovered("extra card " + x.code + " not put back");
        }
    }
    return events;
}

void StandIn::check_after_recovery(const std::string& what, long recoveries_before) {
    if (table_->tracker().counts().recoveries == recoveries_before) return;
    const auto diff = sim_->compare(table_->tracker().belief(), table_->tracker().hand_pile(), true);
    if (!diff.empty()) unrecovered(what + ": after a recovery " + diff);
}

void StandIn::apply(const json& line) {
    Command c = parse_command(line.at("cmd"));
    const auto objs = line.at("objs").get<std::vector<int>>();
    const std::string what = std::string(to_string(c.kind)) + " #" + std::to_string(stats_.commands + 1);
    switch (c.kind) {
        case CommandKind::Play:
        case CommandKind::Move:
        case CommandKind::Remove:
            c.id = id_of(objs.at(0));
            if (c.id < 0) {
                unrecovered(what + ": the table never reported object " + std::to_string(objs.at(0)));
                return;
            }
            break;
        case CommandKind::Exhaust:
            for (const int o : objs) c.ids.push_back(id_of(o));
            break;
        case CommandKind::Token:
            code_[objs.at(0)] = c.code;
            break;
        default:
            break;
    }
    const OpCounts before = table_->tracker().counts();
    for (int attempt = 0;; ++attempt) {
        const long recoveries = table_->tracker().counts().recoveries;
        const auto events = run(c);
        check_after_recovery(what, recoveries);
        const Event& end = events.back();
        if (end.kind == EventKind::Done) {
            if (c.kind == CommandKind::Deal || c.kind == CommandKind::Peek || c.kind == CommandKind::Token) {
                for (std::size_t i = 0; i < objs.size() && i < end.cards.size(); ++i) id_[objs[i]] = end.cards[i].id;
            }
            if (c.kind == CommandKind::Play) {
                ++stats_.plays;
                const OpCounts& now = table_->tracker().counts();
                stats_.feeds.push_back(static_cast<int>(now.feeds - before.feeds));
                stats_.scans.push_back(static_cast<int>(now.scans - before.scans));
                const auto& placed = sim_->placed();
                const std::string& want = code_[objs[0]];
                if (placed.empty() || placed.front() != want) {
                    unrecovered(what + ": played " + (placed.empty() ? "nothing" : placed.front()) + " for " + want);
                }
            }
            if ((c.kind == CommandKind::Play || c.kind == CommandKind::Move) && into_a_deck(c.to)) id_.erase(objs[0]);
            if (c.kind == CommandKind::Remove) id_.erase(objs[0]);
            return;
        }
        const bool retry = end.reason == FailReason::NotInHand || end.reason == FailReason::Unreadable;
        if (attempt == 0 && retry) {
            ++stats_.retries;
            continue;
        }
        unrecovered(what + ": failed " + std::string(to_string(end.reason)) + ": " + end.message);
        return;
    }
}

GameStats StandIn::finish() {
    const auto events = run({.kind = CommandKind::RescanHand});
    if (events.back().kind != EventKind::Done) unrecovered("final rescan failed: " + events.back().message);
    if (auto diff = sim_->compare(table_->tracker().belief(), table_->tracker().hand_pile()); !diff.empty()) {
        unrecovered("at game end: " + diff);
    }
    stats_.ops = table_->tracker().counts();
    stats_.faults = sim_->faults();
    return stats_;
}

SoakReport soak(std::istream& in, const SoakOptions& options, std::ostream* log) {
    SoakReport r;
    std::unique_ptr<StandIn> game;
    std::string text;
    long line_no = 0;
    while (std::getline(in, text)) {
        ++line_no;
        if (text.empty()) continue;
        const json line = json::parse(text);
        if (line.contains("setup")) {
            game = std::make_unique<StandIn>(line["setup"], options, static_cast<std::uint64_t>(r.games));
        } else if (line.contains("cmd")) {
            if (!game) throw std::runtime_error("line " + std::to_string(line_no) + ": command before setup");
            game->apply(line);
        } else if (line.contains("end")) {
            if (!game) throw std::runtime_error("line " + std::to_string(line_no) + ": end before setup");
            GameStats g = game->finish();
            r.unrecovered += static_cast<long>(g.unrecovered.size());
            if (log) {
                for (const auto& u : g.unrecovered) *log << "game " << r.games << ": " << u << "\n";
            }
            r.per_game.push_back(std::move(g));
            ++r.games;
            game.reset();
        } else {
            throw std::runtime_error("line " + std::to_string(line_no) + ": not a workload line");
        }
    }
    return r;
}

void print_report(const SoakReport& r, const SoakOptions& o, std::ostream& out) {
    std::vector<int> feeds, scans, recoveries;
    GameStats sum;
    for (const GameStats& g : r.per_game) {
        feeds.insert(feeds.end(), g.feeds.begin(), g.feeds.end());
        scans.insert(scans.end(), g.scans.begin(), g.scans.end());
        recoveries.push_back(static_cast<int>(g.ops.recoveries));
        sum.commands += g.commands;
        sum.plays += g.plays;
        sum.retries += g.retries;
        sum.corrections += g.corrections;
        sum.replaced += g.replaced;
        sum.extra += g.extra;
        sum.missing += g.missing;
        sum.ops.scans += g.ops.scans;
        sum.ops.feeds += g.ops.feeds;
        sum.ops.takes += g.ops.takes;
        sum.ops.deals += g.ops.deals;
        sum.ops.passes += g.ops.passes;
        sum.ops.recoveries += g.ops.recoveries;
        sum.faults.misread += g.faults.misread;
        sum.faults.unreadable += g.faults.unreadable;
        sum.faults.double_feed += g.faults.double_feed;
        sum.faults.missed_feed += g.faults.missed_feed;
        sum.faults.double_take += g.faults.double_take;
        sum.faults.double_deal += g.faults.double_deal;
        sum.faults.disturbed += g.faults.disturbed;
    }
    const auto per_game = [&](long n) { return r.games ? static_cast<double>(n) / r.games : 0.0; };
    out << std::fixed << std::setprecision(2);
    out << "soak: " << r.games << " games, faults " << to_string(o.faults) << ", confirm_reads "
        << o.tracker.confirm_reads << ", post-check " << (o.tracker.post_check ? "on" : "off") << "\n";
    out << "unrecovered: " << r.unrecovered << "\n";
    out << "commands " << sum.commands << " (" << per_game(sum.commands) << " per game), plays " << sum.plays << " ("
        << per_game(sum.plays) << " per game)\n";
    out << "per play: feeds p50 " << percentile(feeds, 50) << " p95 " << percentile(feeds, 95) << " max "
        << percentile(feeds, 100) << "; scans p50 " << percentile(scans, 50) << " p95 " << percentile(scans, 95)
        << " max " << percentile(scans, 100) << "\n";
    out << "per game: scans " << per_game(sum.ops.scans) << ", feeds " << per_game(sum.ops.feeds) << ", takes "
        << per_game(sum.ops.takes) << ", deals " << per_game(sum.ops.deals) << ", recoveries "
        << per_game(sum.ops.recoveries) << " (max " << percentile(recoveries, 100) << ", incl. the final rescan), "
        << "passes " << per_game(sum.ops.passes) << "\n";
    out << "corrections " << sum.corrections << " (replaced " << sum.replaced << ", extra " << sum.extra
        << ", missing " << sum.missing << "), retries " << sum.retries << "\n";
    out << "faults injected: misread " << sum.faults.misread << ", unreadable " << sum.faults.unreadable
        << ", double feed " << sum.faults.double_feed << ", missed feed " << sum.faults.missed_feed << ", double take "
        << sum.faults.double_take << ", double deal " << sum.faults.double_deal << ", disturbed "
        << sum.faults.disturbed << "\n";
}

} // namespace buddy::table
