#include "card_db.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <tuple>

#include <nlohmann/json.hpp>

#include "buddy/names.hpp"

namespace buddy::riftbound {

using json = nlohmann::json;

namespace {

std::optional<CardType> parse_type(std::string_view s) {
    if (s == "Legend") return CardType::Legend;
    if (s == "Unit") return CardType::Unit;
    if (s == "Spell") return CardType::Spell;
    if (s == "Gear") return CardType::Gear;
    if (s == "Rune") return CardType::Rune;
    if (s == "Battlefield") return CardType::Battlefield;
    return std::nullopt;
}

std::optional<Domains> parse_domain(std::string_view s) {
    if (s == "Fury") return domain::fury;
    if (s == "Calm") return domain::calm;
    if (s == "Mind") return domain::mind;
    if (s == "Body") return domain::body;
    if (s == "Chaos") return domain::chaos;
    if (s == "Order") return domain::order;
    if (s == "Colorless") return Domains{0};
    return std::nullopt;
}

std::optional<int> opt_int(const json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number_integer()) return std::nullopt;
    return it->get<int>();
}

std::string str(const json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

struct SetInfo {
    std::size_t order;  // release order
    bool promo;
    std::string name;
};

// What printings of one card must agree on.
auto rules_fields(const Card& c) {
    return std::tie(c.type, c.champion, c.signature, c.token, c.domains, c.energy, c.might, c.power);
}

std::string slash_total(int total) {  // "/024", as printed
    std::string digits = std::to_string(total);
    return "/" + std::string(digits.size() < 3 ? 3 - digits.size() : 0, '0') + digits;
}

std::string describe(const Card& c) {
    std::string s = std::string(to_string(c.type)) + " " + domains_string(c.domains);
    const auto stat = [&](const char* name, const std::optional<int>& v) {
        if (v) s += " " + std::string(name) + std::to_string(*v);
    };
    stat("energy ", c.energy);
    stat("might ", c.might);
    stat("power ", c.power);
    if (c.champion) s += " champion";
    if (c.signature) s += " signature";
    if (c.token) s += " token";
    return s;
}

} // namespace

std::string_view to_string(CardType type) {
    switch (type) {
    case CardType::Legend: return "Legend";
    case CardType::Unit: return "Unit";
    case CardType::Spell: return "Spell";
    case CardType::Gear: return "Gear";
    case CardType::Rune: return "Rune";
    case CardType::Battlefield: return "Battlefield";
    }
    return "?";
}

std::string domains_string(Domains d) {
    static constexpr std::pair<Domains, const char*> names[] = {
        {domain::fury, "Fury"}, {domain::calm, "Calm"},   {domain::mind, "Mind"},
        {domain::body, "Body"}, {domain::chaos, "Chaos"}, {domain::order, "Order"},
    };
    std::string s;
    for (const auto& [bit, name] : names) {
        if ((d & bit) == 0) continue;
        if (!s.empty()) s += '+';
        s += name;
    }
    return s.empty() ? "Colorless" : s;
}

std::string_view to_string(Timing timing) {
    switch (timing) {
    case Timing::Normal: return "Normal";
    case Timing::Action: return "Action";
    case Timing::Reaction: return "Reaction";
    }
    return "?";
}

Timing printed_timing(std::string_view text) {
    for (const auto& [keyword, timing] : {std::pair{std::string_view{"[Action]"}, Timing::Action},
                                         std::pair{std::string_view{"[Reaction]"}, Timing::Reaction}}) {
        if (!text.starts_with(keyword)) continue;
        const auto rest = text.substr(keyword.size());
        if (rest.starts_with("[>]") || rest.starts_with("[&gt;]")) return Timing::Normal;  // ability (Riftcodex escapes >)
        return timing;
    }
    return Timing::Normal;
}

bool Card::has_tag(std::string_view tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

std::string card_name(std::string_view riftcodex_name) {
    std::string name(riftcodex_name);
    // "… (Alternate Art)", "… (Signature) (Metal)"; not "Sprite (274) // Buff" (a token's own name)
    while (name.size() > 2 && name.back() == ')') {
        const auto open = name.rfind(" (");
        if (open == std::string::npos) break;
        name.erase(open);
    }
    const auto dash = name.find(" - ");
    if (dash != std::string::npos && name.rfind(", ", dash) == std::string::npos) name.replace(dash, 3, ", ");
    return name;
}

CardDb CardDb::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("can't open " + path + " (fetch it: deploy/pi/fetch-cards.sh)");
    try {
        return parse(json::parse(in));
    } catch (const std::exception& e) {
        throw std::runtime_error(path + ": " + e.what());
    }
}

CardDb CardDb::parse(const json& doc) {
    if (!doc.is_object() || !doc.contains("printings") || !doc["printings"].is_array()) {
        throw std::runtime_error("not a card file: no \"printings\" list");
    }
    CardDb db;
    db.fetched_ = str(doc, "fetched");

    std::map<std::string, SetInfo, std::less<>> sets;
    if (const auto it = doc.find("sets"); it != doc.end() && it->is_array()) {
        for (const auto& s : *it) {
            const std::string name = str(s, "name");
            sets[str(s, "id")] = {sets.size(), name.find("Promotional") != std::string::npos, name};
        }
    }

    // Read every printing, grouped by name key.
    std::map<std::string, std::vector<Card>> groups;
    for (const auto& p : doc["printings"]) {
        const std::string id = str(p, "id");
        const auto code = parse_code(id);
        const auto type = parse_type(str(p, "type"));
        if (!code || !type) {
            db.warnings_.push_back("skipped " + id + ": " + (!code ? "unreadable id" : "unknown type " + str(p, "type")));
            continue;
        }
        Card c;
        const std::string full_name = str(p, "name");
        c.name = card_name(full_name);
        c.type = *type;
        const std::string super = str(p, "supertype");
        c.champion = c.type == CardType::Unit && super == "Champion";
        c.signature = super == "Signature";
        c.token = super == "Token";
        if (const auto it = p.find("domains"); it != p.end() && it->is_array()) {
            for (const auto& d : *it) {
                const auto bit = d.is_string() ? parse_domain(d.get<std::string>()) : std::nullopt;
                if (!bit) db.warnings_.push_back(id + ": unknown domain " + d.dump());
                c.domains |= bit.value_or(0);
            }
        }
        c.energy = opt_int(p, "energy");
        c.might = opt_int(p, "might");
        c.power = opt_int(p, "power");
        if (const auto it = p.find("tags"); it != p.end() && it->is_array()) {
            for (const auto& t : *it) {
                if (t.is_string()) c.tags.push_back(t.get<std::string>());
            }
        }
        c.text = str(p, "text");
        c.timing = printed_timing(c.text);
        const auto set = sets.find(code->set);
        c.printings.push_back({.code = *code, .label = {}, .set_name = set != sets.end() ? set->second.name : code->set,
                                .full_name = full_name});
        groups[names::squash(c.name)].push_back(std::move(c));
    }

    // Home printing first: plain over variant, main set over promo, older set, lower number (overnumbered last).
    const auto rank = [&](const Card& c) {
        const Code& code = c.home().code;
        const auto set = sets.find(code.set);
        const bool known = set != sets.end();
        return std::make_tuple(code.variant != 0, !known || set->second.promo, known ? set->second.order : sets.size(),
                               code.number, code.str());
    };
    for (auto& [key, printings] : groups) {
        std::sort(printings.begin(), printings.end(),
                  [&](const Card& a, const Card& b) { return rank(a) < rank(b); });
        Card card = printings.front();
        for (std::size_t i = 1; i < printings.size(); ++i) {
            const Card& other = printings[i];
            card.printings.push_back(other.home());
            for (const auto& tag : other.tags) {
                if (!card.has_tag(tag)) card.tags.push_back(tag);
            }
            if (rules_fields(other) != rules_fields(card) && card.conflict.empty()) {
                card.conflict = card.home().code.str() + " is " + describe(card) + ", " + other.home().code.str() +
                                " is " + describe(other);
            }
        }
        db.cards_.push_back(std::move(card));
    }
    std::sort(db.cards_.begin(), db.cards_.end(), [](const Card& a, const Card& b) { return a.name < b.name; });
    for (std::size_t i = 0; i < db.cards_.size(); ++i) {
        db.by_name_[names::squash(db.cards_[i].name)] = i;
        for (std::size_t j = 0; j < db.cards_[i].printings.size(); ++j) {
            db.by_code_.emplace(db.cards_[i].printings[j].code.str(), std::pair{i, j});
        }
    }
    for (auto& card : db.cards_) {
        for (auto& p : card.printings) {
            p.label = p.code.str();
            if (db.by_code_.count(p.label) > 1) p.label += slash_total(p.code.total);
        }
    }
    return db;
}

CardDb::Found CardDb::find_code(std::string_view text) const {
    const auto code = parse_code(text);
    if (!code) return {.error = "\"" + std::string(text) + "\" is not a card code (like OGN-007a/298)"};
    const auto [first, last] = by_code_.equal_range(code->str());
    if (first == last) return {.error = "unknown card code " + code->str()};
    std::vector<std::pair<const Card*, const Printing*>> matches;  // total fits or unknown
    std::string others;
    for (auto it = first; it != last; ++it) {
        const Card& card = cards_[it->second.first];
        const Printing& p = card.printings[it->second.second];
        if (code->total == 0 || p.code.total == 0 || code->total == p.code.total) matches.emplace_back(&card, &p);
        others += (others.empty() ? "" : ", ") + p.label + " is " + card.name;
    }
    if (matches.empty()) {
        return {.error = code->str() + slash_total(code->total) + ": no such printing (" + others +
                         "), misread?"};
    }
    if (matches.size() > 1) return {.error = code->str() + " is ambiguous (" + others + "): give the /total"};
    return {.card = matches[0].first, .printing = matches[0].second, .error = {}};
}

const Card* CardDb::find_name(std::string_view name) const {
    const auto it = by_name_.find(names::squash(std::string(name)));
    return it == by_name_.end() ? nullptr : &cards_[it->second];
}

std::vector<std::string> CardDb::similar_names(std::string_view name, std::size_t max) const {
    const std::string key = names::squash(std::string(name));
    const std::size_t limit = std::max<std::size_t>(2, key.size() / 4);
    std::vector<std::pair<std::size_t, const std::string*>> close;
    for (const auto& card : cards_) {
        const std::size_t d = names::edit_distance(key, names::squash(card.name));
        if (d <= limit) close.emplace_back(d, &card.name);
    }
    std::sort(close.begin(), close.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first < b.first : *a.second < *b.second;
    });
    std::vector<std::string> out;
    for (std::size_t i = 0; i < close.size() && i < max; ++i) out.push_back(*close[i].second);
    return out;
}

} // namespace buddy::riftbound
