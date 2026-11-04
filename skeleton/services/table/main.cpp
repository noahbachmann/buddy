// table: Buddy's physical Riftbound cards (Phase 5, 5.5). Piles, hand order, scans and recovery behind
// buddy/table/cmd; the sim backend until the hardware exists (5.7+).
//
//   buddy-table --soak <file|-> [--faults <rates>] [--confirm-reads N] [--post-check on|off] [--seed S]
//                    plays a workload (buddy-game --table-workload) against the sim, standing in for game:
//                    reports unrecovered states, feeds and scans per play, recoveries, the faults injected;
//                    exit 1 if anything wasn't recovered. Rates: "stress", "none" or misread=0.02,… (sim.hpp)
//
// Config (env, see deploy/config/buddy.env.example): BUDDY_TABLE_*
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "buddy/config.hpp"
#include "sim.hpp"
#include "soak.hpp"

namespace {

using namespace buddy;
using namespace buddy::table;

int usage(const char* argv0) {
    std::cerr << "usage: " << argv0
              << " --soak <file|-> [--faults <rates>] [--confirm-reads N] [--post-check on|off] [--seed S]\n";
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    std::string soak_file;
    SoakOptions options;
    try {
        options.faults = parse_faults(env_string("BUDDY_TABLE_SIM_FAULTS", ""));
        options.tracker.confirm_reads = env_int("BUDDY_TABLE_CONFIRM_READS", options.tracker.confirm_reads);
        options.tracker.post_check = env_int("BUDDY_TABLE_POST_CHECK", options.tracker.post_check ? 1 : 0) != 0;
        for (std::size_t i = 0; i < args.size(); ++i) {
            const bool value = i + 1 < args.size();
            if (args[i] == "--soak" && value) soak_file = args[++i];
            else if (args[i] == "--faults" && value) options.faults = parse_faults(args[++i]);
            else if (args[i] == "--confirm-reads" && value) options.tracker.confirm_reads = std::stoi(args[++i]);
            else if (args[i] == "--post-check" && value) {
                const std::string v = args[++i];
                if (v != "on" && v != "off") return usage(argv[0]);
                options.tracker.post_check = v == "on";
            } else if (args[i] == "--seed" && value) options.seed = std::stoull(args[++i]);
            else return usage(argv[0]);
        }
    } catch (const std::exception& e) {
        std::cerr << "✗ " << e.what() << "\n";
        return usage(argv[0]);
    }
    if (soak_file.empty() || options.tracker.confirm_reads < 1) return usage(argv[0]);

    try {
        std::ifstream file;
        if (soak_file != "-") {
            file.open(soak_file);
            if (!file) throw std::runtime_error("can't read " + soak_file);
        }
        const SoakReport r = soak(soak_file == "-" ? std::cin : file, options, &std::cerr);
        print_report(r, options, std::cout);
        return r.unrecovered == 0 ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "✗ " << e.what() << "\n";
        return 1;
    }
}
