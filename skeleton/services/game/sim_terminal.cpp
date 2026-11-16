#include "sim_terminal.hpp"

#include <algorithm>
#include <format>
#include <sstream>
#include <stdexcept>

namespace buddy::game {

using nlohmann::json;

namespace {

std::string who(const json& p) {
    if (!p.is_string()) return "nobody";
    return p == "buddy" ? "Buddy" : "Dev";
}

struct Text {
    const json& cat;

    std::string name(const json& code) const {
        if (!code.is_string()) return "a hidden card";
        const auto it = cat.find(code.get<std::string>());
        return it == cat.end() ? code.get<std::string>() : (*it)["name"].get<std::string>();
    }

    std::string object(const json& o) const {
        if (o.value("hidden", false)) return "a facedown card";
        std::vector<std::string> notes;
        if (o.contains("might")) notes.push_back(std::format("might {}", o["might"].get<int>()));
        if (o.value("damage", 0) > 0) notes.push_back(std::format("{} damage", o["damage"].get<int>()));
        if (o.value("buffs", 0) > 0) notes.push_back(std::format("+{} buff", o["buffs"].get<int>()));
        if (o.value("exhausted", false)) notes.push_back("exhausted");
        if (o.value("stunned", false)) notes.push_back("stunned");
        if (o.value("empowered", false)) notes.push_back("empowered");
        if (o.contains("attached_to")) notes.push_back(std::format("on #{}", o["attached_to"].get<int>()));
        if (o.contains("keywords")) {
            for (const auto& k : o["keywords"]) notes.push_back(k.get<std::string>());
        }
        std::string s = std::format("{} #{}", name(o["card"]), o["id"].get<int>());
        if (!notes.empty()) {
            s += " (";
            for (std::size_t i = 0; i < notes.size(); ++i) s += (i ? ", " : "") + notes[i];
            s += ")";
        }
        return s;
    }

    std::string list(const json& objects) const {
        if (objects.empty()) return "-";
        std::string s;
        for (const auto& o : objects) s += (s.empty() ? "" : ", ") + object(o);
        return s;
    }

    std::string player(const std::string& label, const json& p) const {
        std::ostringstream out;
        out << std::format("{}: {} points · hand {} · deck {} · rune deck {}\n", label, p["points"].get<int>(),
                           p["hand"].get<int>(), p["deck"].get<int>(), p["rune_deck"].get<int>());
        out << "  legend: " << (p["legend"].is_null() ? "-" : object(p["legend"])) << "\n";
        if (!p["champion"].empty()) out << "  champion zone: " << list(p["champion"]) << "\n";
        out << "  base: " << list(p["base"]) << "\n";
        std::string runes;
        for (const auto& r : p["runes"]) {
            runes += std::format("{}{} {} ready", runes.empty() ? "" : ", ", r["domain"].get<std::string>(),
                                 r["ready"].get<int>());
            if (r["exhausted"].get<int>() > 0) runes += std::format(" + {} exhausted", r["exhausted"].get<int>());
        }
        out << "  runes: " << (runes.empty() ? "-" : runes) << "\n";
        const json& pool = p["pool"];
        std::string pl;
        if (pool["energy"].get<int>() > 0) pl += std::format("{} energy", pool["energy"].get<int>());
        for (const auto& [d, n] : pool["power"].items()) pl += std::format("{}{} {}", pl.empty() ? "" : ", ", n.get<int>(), d);
        if (pool["any"].get<int>() > 0) pl += std::format("{}{} any power", pl.empty() ? "" : ", ", pool["any"].get<int>());
        if (!pl.empty()) out << "  pool: " << pl << "\n";
        for (const char* pile : {"trash", "banishment"}) {
            if (p[pile].empty()) continue;
            std::string s;
            for (const auto& c : p[pile]) s += (s.empty() ? "" : ", ") + name(c);
            out << "  " << pile << ": " << s << "\n";
        }
        return out.str();
    }
};

} // namespace

std::string render(const json& p, int log_tail) {
    if (p.value("status", "") == "idle") return "no game\n";
    const json empty = json::object();
    const Text t{p.contains("catalogue") ? p["catalogue"] : empty};
    std::ostringstream out;
    const json& players = p["players"];
    out << std::format("\n── turn {} · {} · {}'s turn · Buddy {} : {} Dev (to {}) ──\n", p["turn"].get<int>(),
                       p["phase"].get<std::string>(), who(p["turn_player"]), players["buddy"]["points"].get<int>(),
                       players["dev"]["points"].get<int>(), p["victory"].get<int>());
    out << t.player("Buddy", players["buddy"]);
    out << "Battlefields:\n";
    for (std::size_t i = 0; i < p["battlefields"].size(); ++i) {
        const json& b = p["battlefields"][i];
        out << std::format("  {} ({}'s): held by {}", t.name(b["card"]), who(b["owner"]), who(b["controller"]));
        if (!b["contested"].is_null()) out << ", contested by " << who(b["contested"]);
        if (b["facedown"].get<int>() > 0) out << std::format(", {} facedown", b["facedown"].get<int>());
        out << "\n    Buddy: " << t.list(b["units"]["buddy"]) << "\n    Dev:   " << t.list(b["units"]["dev"]) << "\n";
    }
    out << t.player("Dev", players["dev"]);
    if (p.contains("showdown")) {
        const json& s = p["showdown"];
        out << std::format("{} at {}, focus {}", s["combat"].get<bool>() ? "Combat" : "Showdown",
                           t.name(p["battlefields"][s["battlefield"].get<std::size_t>()]["card"]), who(s["focus"]));
        if (!s["attacker"].is_null()) out << ", attacker " << who(s["attacker"]);
        out << "\n";
    }
    if (!p["chain"].empty()) {
        out << "Chain (newest last):\n";
        for (const auto& c : p["chain"]) {
            out << std::format("  {} {}{}", who(c["controller"]), t.name(c["card"]),
                               c["kind"] == "card" ? "" : " (" + c["kind"].get<std::string>() + ")");
            std::string targets;
            for (const auto& x : c["targets"]) {
                targets += (targets.empty() ? "" : ", ") + (x.contains("player") ? who(x["player"]) : t.name(x["card"]));
            }
            if (!targets.empty()) out << " → " << targets;
            out << "\n";
        }
    }
    const json& log = p["log"];
    const std::size_t from = log.size() > static_cast<std::size_t>(log_tail) ? log.size() - static_cast<std::size_t>(log_tail) : 0;
    if (from < log.size()) out << "Log:\n";
    for (std::size_t i = from; i < log.size(); ++i) {
        out << std::format("  t{} {}: {}\n", log[i]["turn"].get<int>(), who(log[i]["player"]), log[i]["text"].get<std::string>());
    }
    const std::string status = p["status"];
    if (status == "over") {
        out << "Game over: " << (p["winner"].is_null() ? "no winner" : who(p["winner"]) + " wins") << "\n";
    } else if (status == "paused") {
        out << "Paused: " << p["pause"]["message"].get<std::string>() << "\n";
        if (p["pause"]["reason"] != "desync") out << "  type resume (after fixing the table) or concede\n";
        else out << "  the game can't go on: concede\n";
    } else if (p.contains("options")) {
        const std::string purpose = p["decision"]["purpose"];
        out << "Your " << p["decision"]["kind"].get<std::string>() << (purpose == "none" ? "" : " (" + purpose + ")") << ":\n";
        for (const auto& o : p["options"]) out << std::format("  [{}] {}\n", o["i"].get<int>(), o["text"].get<std::string>());
    } else if (p.contains("prompt")) {
        const json& pr = p["prompt"];
        const int n = pr["n"].get<int>();
        out << std::format("Name {} card{} ({}): candidate numbers or codes\n", n, n == 1 ? "" : "s",
                           pr["kind"].get<std::string>());
        for (std::size_t i = 0; i < pr["candidates"].size(); ++i) {
            const json& c = pr["candidates"][i];
            out << std::format("  [{}] {} ({}){} · {} left\n", i, t.name(c["code"]), c["code"].get<std::string>(),
                               c.contains("domain") ? ", " + c["domain"].get<std::string>() : "", c["left"].get<int>());
        }
    }
    return out.str();
}

std::vector<std::string> prompt_answer(const json& p, const std::string& line) {
    if (!p.contains("prompt")) throw std::invalid_argument("nothing to name");
    const json& candidates = p["prompt"]["candidates"];
    std::string spaced = line;
    std::replace(spaced.begin(), spaced.end(), ',', ' ');
    std::istringstream in(spaced);
    std::vector<std::string> out;
    for (std::string word; in >> word;) {
        if (std::all_of(word.begin(), word.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            const auto i = word.size() > 4 ? candidates.size() : std::stoul(word);
            if (i >= candidates.size()) throw std::invalid_argument("no candidate " + word);
            out.push_back(candidates[i]["code"]);
        } else {
            out.push_back(word);  // a code: the host checks it
        }
    }
    if (out.size() != p["prompt"]["n"].get<std::size_t>()) {
        throw std::invalid_argument(std::format("name {} cards, not {}", p["prompt"]["n"].get<int>(), out.size()));
    }
    return out;
}

TerminalGame::TerminalGame(const riftbound::CardDb& db, HostConfig cfg, TerminalOptions o)
    : host_(db, std::move(cfg)), o_(std::move(o)), sim_({}, o_.faults, o_.seed * 7 + 3), table_(sim_, {}) {}

bool TerminalGame::over() const { return host_.session() && host_.session()->status() == Status::Over; }

std::string TerminalGame::start() {
    return send({{"action", "new"}, {"buddy", o_.buddy_deck}, {"dev", "human"}, {"seed", o_.seed}});
}

std::string TerminalGame::send(const json& cmd) {
    const Reply r = host_.command(cmd);
    if (!r.ok) return "✗ " + r.message + "\n";
    return pump();
}

std::string TerminalGame::pump() {
    for (long guard = 0; guard < 1'000'000; ++guard) {
        HostOutputs o = host_.take();
        if (o.new_game) {
            const auto& cfg = host_.session()->config();
            o_.buddy_ai->begin(cfg.buddy, cfg.decks[0], cfg.decks[1]);
        }
        voice_.insert(voice_.end(), o.voice.begin(), o.voice.end());
        pending_.insert(pending_.end(), o.table.begin(), o.table.end());
        if (!pending_.empty()) {
            const table::Command c = pending_.front();
            pending_.erase(pending_.begin());
            if (c.kind == table::CommandKind::Setup) sim_.configure({c.sim_deck, c.sim_runes, c.legend, c.champion, true});
            for (const auto& e : table_.execute(c)) host_.on_table(e);
            continue;
        }
        if (o.ai) {
            host_.on_ai(o.ai->id, o_.buddy_ai->choose(o.ai->view, o.ai->actions));
            continue;
        }
        break;
    }
    std::string out;
    for (const auto& v : voice_) out += "Buddy: \"" + v + "\"\n";
    voice_.clear();
    return out + render(host_.state());
}

std::string TerminalGame::input(const std::string& raw) {
    const auto first = raw.find_first_not_of(" \t");
    const std::string line = first == std::string::npos ? "" : raw.substr(first, raw.find_last_not_of(" \t\r") - first + 1);
    const json& st = host_.state();
    if (line.empty()) return "";
    if (line == "help") {
        return "  <n>            choose option n\n"
               "  <n> <m> …      at a prompt: the candidates' numbers (or card codes), one per card\n"
               "  board          print the board again\n"
               "  resume         after a pause, once the table is in order\n"
               "  concede        give up the game\n"
               "  quit           leave\n";
    }
    if (line == "board") return render(st);
    if (line == "resume" || line == "concede") return send({{"action", line}});
    if (st.contains("prompt")) {
        try {
            return send({{"action", "reveal"}, {"v", st["v"]}, {"cards", prompt_answer(st, line)}});
        } catch (const std::invalid_argument& e) {
            return "✗ " + std::string(e.what()) + "\n";
        }
    }
    if (st.contains("options") && std::all_of(line.begin(), line.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        if (line.size() > 4) return "✗ no option " + line + "\n";
        return send({{"action", "act"}, {"v", st["v"]}, {"i", std::stoul(line)}});
    }
    return "✗ type an option number, or help\n";
}

} // namespace buddy::game
