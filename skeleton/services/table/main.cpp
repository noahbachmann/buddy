// table: Buddy's physical Riftbound cards (Phase 5, 5.5). Piles, hand order, scans and recovery behind
// buddy/table/cmd; the sim backend until the hardware exists (5.7+). Protocol: shared/table_protocol.hpp; how it
// works: services/table/CLAUDE.md.
//
//   buddy-table                      live: buddy/table/cmd in, buddy/table/event out (one done/failed per seq),
//                                    retained buddy/table/debug (the belief: Buddy's hand, so not a /state topic
//                                    the web panel would stream)
//   buddy-table --cmd '<json>'…      test: commands in order on a fresh sim (seq optional), events and the belief
//                                    printed, no MQTT
//   buddy-table --soak <file|-> [--faults <rates>] [--confirm-reads N] [--post-check on|off] [--seed S]
//                                    plays a workload (buddy-game --table-workload) against the sim, standing in
//                                    for game: unrecovered states, feeds and scans per play, recoveries, faults
//                                    injected; exit 1 if anything wasn't recovered. Rates: "stress", "none" or
//                                    misread=0.02,… (sim.hpp)
//
// Config (env, see deploy/config/buddy.env.example): BUDDY_TABLE_*
#include <chrono>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "buddy/config.hpp"
#include "buddy/lifecycle.hpp"
#include "buddy/mqtt.hpp"
#include "buddy/queue.hpp"
#include "buddy/systemd.hpp"
#include "buddy/topics.hpp"
#include "sim.hpp"
#include "soak.hpp"
#include "table.hpp"

namespace {

using namespace buddy;
using namespace buddy::table;
using json = nlohmann::json;

struct Config {
    std::string backend = env_string("BUDDY_TABLE_BACKEND", "sim");
    SimFaults faults = parse_faults(env_string("BUDDY_TABLE_SIM_FAULTS", ""));
    TrackerConfig tracker = [] {
        TrackerConfig t;
        t.confirm_reads = env_int("BUDDY_TABLE_CONFIRM_READS", t.confirm_reads);
        t.post_check = env_int("BUDDY_TABLE_POST_CHECK", t.post_check ? 1 : 0) != 0;
        return t;
    }();
    std::uint64_t seed = static_cast<std::uint64_t>(env_int("BUDDY_TABLE_SIM_SEED", 0));  // 0 = random
};

// The sim lays out what the setup command says: its decks' codes if game sent them, else placeholders.
SimSetup sim_setup(const Command& c) {
    SimSetup s;
    s.deck = c.sim_deck;
    s.runes = c.sim_runes;
    for (int i = static_cast<int>(s.deck.size()); i < c.deck; ++i) s.deck.push_back("SIM-" + std::to_string(i + 1));
    for (int i = static_cast<int>(s.runes.size()); i < c.runes; ++i) s.runes.push_back("SIM-R" + std::to_string(i % 2 + 1));
    s.legend = c.legend;
    s.champion = c.champion;
    s.shuffle = true;
    return s;
}

// One payload → its events. A command that doesn't parse still gets its `failed` if it has a seq.
std::vector<Event> handle(Table& table, Sim& sim, const std::string& payload) {
    try {
        const Command c = parse_payload(payload);
        if (c.kind == CommandKind::Setup) sim.configure(sim_setup(c));
        return table.execute(c);
    } catch (const ProtocolError& e) {
        const json j = json::parse(payload, nullptr, /*allow_exceptions=*/false);
        const bool has_seq = j.is_object() && j.contains("seq") && j["seq"].is_number_integer();
        Event f{.kind = EventKind::Failed, .seq = has_seq ? j["seq"].get<std::int64_t>() : -1,
                .reason = FailReason::Invalid, .message = e.what()};
        return {f};
    }
}

void log_events(const std::vector<Event>& events) {
    for (const Event& e : events) {
        if (e.kind == EventKind::Done) continue;  // quiet in steady state (microSD)
        std::cout << "[table] " << to_json(e).dump() << std::endl;
    }
}

int offline(const std::vector<std::string>& commands, const Config& cfg) {
    Sim sim({}, cfg.faults, cfg.seed ? cfg.seed : 1);
    Table table(sim, cfg.tracker);
    std::int64_t seq = 0;
    for (const auto& text : commands) {
        json j = json::parse(text, nullptr, /*allow_exceptions=*/false);
        if (j.is_object() && !j.contains("seq")) j["seq"] = seq + 1;
        if (j.is_object() && j["seq"].is_number_integer()) seq = j["seq"].get<std::int64_t>();
        for (const Event& e : handle(table, sim, j.is_discarded() ? text : j.dump())) {
            std::cout << to_json(e).dump() << "\n";
        }
    }
    std::cout << table.state().dump(2) << "\n";
    return 0;
}

int soak_file(const std::string& file_name, const SoakOptions& options) {
    std::ifstream file;
    if (file_name != "-") {
        file.open(file_name);
        if (!file) throw std::runtime_error("can't read " + file_name);
    }
    const SoakReport r = soak(file_name == "-" ? std::cin : file, options, &std::cerr);
    print_report(r, options, std::cout);
    return r.unrecovered == 0 ? 0 : 1;
}

int usage(const char* argv0) {
    std::cerr << "usage: " << argv0 << "                       (service)\n"
              << "       " << argv0 << " --cmd '<json>'…\n"
              << "       " << argv0
              << " --soak <file|-> [--faults <rates>] [--confirm-reads N] [--post-check on|off] [--seed S]\n";
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    install_signal_handlers();
    const std::vector<std::string> args(argv + 1, argv + argc);
    std::optional<Config> cfg;
    try {
        cfg.emplace();
        if (cfg->backend != "sim") throw std::invalid_argument("BUDDY_TABLE_BACKEND: only \"sim\" exists until 5.7");
        if (cfg->tracker.confirm_reads < 1) throw std::invalid_argument("BUDDY_TABLE_CONFIRM_READS must be ≥ 1");
    } catch (const std::exception& e) {
        std::cerr << "[table] " << e.what() << std::endl;
        return 1;
    }

    if (!args.empty() && args[0] == "--cmd") {
        if (args.size() < 2) return usage(argv[0]);
        return offline({args.begin() + 1, args.end()}, *cfg);
    }
    if (!args.empty() && args[0] == "--soak") {
        SoakOptions options{.faults = cfg->faults, .tracker = cfg->tracker, .seed = cfg->seed};
        std::string file;
        try {
            for (std::size_t i = 0; i < args.size(); ++i) {
                const bool value = i + 1 < args.size();
                if (args[i] == "--soak" && value) file = args[++i];
                else if (args[i] == "--faults" && value) options.faults = parse_faults(args[++i]);
                else if (args[i] == "--confirm-reads" && value) options.tracker.confirm_reads = std::stoi(args[++i]);
                else if (args[i] == "--post-check" && value && (args[i + 1] == "on" || args[i + 1] == "off"))
                    options.tracker.post_check = args[++i] == "on";
                else if (args[i] == "--seed" && value) options.seed = std::stoull(args[++i]);
                else return usage(argv[0]);
            }
            if (file.empty() || options.tracker.confirm_reads < 1) return usage(argv[0]);
            return soak_file(file, options);
        } catch (const std::exception& e) {
            std::cerr << "✗ " << e.what() << "\n";
            return 1;
        }
    }
    if (!args.empty()) return usage(argv[0]);

    const std::uint64_t seed = cfg->seed ? cfg->seed : std::random_device{}();
    Sim sim({}, cfg->faults, seed);
    Table table(sim, cfg->tracker);
    std::cout << "[table] sim backend, faults " << to_string(cfg->faults) << ", confirm_reads "
              << cfg->tracker.confirm_reads << ", post-check " << (cfg->tracker.post_check ? "on" : "off") << std::endl;

    BlockingQueue<std::string> queue;
    on_stop([&] { queue.close(); });
    Mqtt mqtt("table", mqtt_config_from_env());
    mqtt.subscribe(topic::table_cmd, [&](const std::string&, const std::string& payload) { queue.push(payload); });
    if (!mqtt.start()) return 0;
    mqtt.publish(topic::table_debug, table.state().dump(), 1, true);

    while (running()) {
        systemd::notify_watchdog();
        const auto payload = queue.pop(std::chrono::seconds(5));
        if (!payload) continue;
        const auto events = handle(table, sim, *payload);
        for (const Event& e : events) mqtt.publish(topic::table_event, to_json(e).dump(), 1);
        log_events(events);
        mqtt.publish(topic::table_debug, table.state().dump(), 1, true);
    }
    mqtt.stop();
    return 0;
}
