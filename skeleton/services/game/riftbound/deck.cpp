#include "deck.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>

#include "buddy/names.hpp"

namespace buddy::riftbound {

namespace {

enum class Section { None, Legend, Champion, Main, Battlefields, Runes, Sideboard };

std::optional<Section> parse_header(std::string_view line) {
    if (line.empty() || line.back() != ':') return std::nullopt;
    const std::string key = names::squash(std::string(line));
    if (key == "legend") return Section::Legend;
    if (key == "champion") return Section::Champion;
    if (key == "maindeck" || key == "main") return Section::Main;
    if (key == "battlefields" || key == "battlefield") return Section::Battlefields;
    if (key == "runes" || key == "rune" || key == "runedeck") return Section::Runes;
    if (key == "sideboard") return Section::Sideboard;
    return Section::None;  // a header, but not one we know
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

std::string quoted(std::string_view s) { return "\"" + std::string(s) + "\""; }

struct Parsed {
    std::vector<DeckEntry> legend, champion, main, battlefields, runes;
};

} // namespace

int Deck::main_size() const {
    int n = champion != nullptr ? 1 : 0;
    for (const auto& e : main) n += e.count;
    return n;
}

int Deck::rune_count() const {
    int n = 0;
    for (const auto& e : runes) n += e.count;
    return n;
}

DeckError::DeckError(std::vector<std::string> problems)
    : std::runtime_error([&] {
          std::string all;
          for (const auto& p : problems) all += (all.empty() ? "" : "\n") + p;
          return all;
      }()),
      problems_(std::move(problems)) {}

Deck parse_deck(const CardDb& db, std::string_view text) {
    std::vector<std::string> problems;
    Parsed parsed;
    Section section = Section::None;
    bool any_header = false;

    int line_no = 0;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t end = std::min(text.find('\n', pos), text.size());
        const std::string_view line = trim(text.substr(pos, end - pos));
        pos = end + 1;
        ++line_no;
        const std::string at = "line " + std::to_string(line_no) + ": ";
        if (line.empty()) continue;

        if (const auto header = parse_header(line)) {
            if (*header == Section::None) problems.push_back(at + "unknown section " + quoted(line));
            section = *header;
            any_header = true;
            continue;
        }

        // "<count> <name or code>", also "<count>x <name>"
        int count = 0;
        const auto [rest, ec] = std::from_chars(line.data(), line.data() + line.size(), count);
        std::string_view what(rest, static_cast<std::size_t>(line.data() + line.size() - rest));
        if (!what.empty() && (what.front() == 'x' || what.front() == 'X')) what.remove_prefix(1);
        what = trim(what);
        if (ec != std::errc{} || count < 1 || count > 99 || what.empty()) {
            problems.push_back(at + "expected \"<count> <card name>\", got " + quoted(line));
            continue;
        }
        if (section == Section::Sideboard) continue;
        if (section == Section::None) {
            if (!any_header) problems.push_back(at + "card before the first section (Legend:, MainDeck:, …)");
            continue;
        }

        const Card* card = db.find_name(what);
        if (card == nullptr) {
            const auto found = db.find_code(what);
            card = found.card;
            if (card == nullptr && parse_code(what)) {
                problems.push_back(at + found.error);
                continue;
            }
        }
        if (card == nullptr) {
            std::string p = at + "unknown card " + quoted(what);
            const auto similar = db.similar_names(what);
            for (std::size_t i = 0; i < similar.size(); ++i) p += (i == 0 ? " (did you mean " : " or ") + quoted(similar[i]);
            problems.push_back(similar.empty() ? p : p + "?)");
            continue;
        }
        if (!card->conflict.empty()) {
            problems.push_back(at + card->name + ": its printings disagree (" + card->conflict +
                               "), check the card data");
        }

        auto& list = section == Section::Legend         ? parsed.legend
                     : section == Section::Champion     ? parsed.champion
                     : section == Section::Main         ? parsed.main
                     : section == Section::Battlefields ? parsed.battlefields
                                                        : parsed.runes;
        const auto same = std::find_if(list.begin(), list.end(), [&](const DeckEntry& e) { return e.card == card; });
        if (same != list.end()) same->count += count;
        else list.push_back({card, count});
    }

    // Shape of each section.
    Deck deck;
    const auto total = [](const std::vector<DeckEntry>& list) {
        int n = 0;
        for (const auto& e : list) n += e.count;
        return n;
    };
    const auto wrong_type = [&](const std::vector<DeckEntry>& list, std::string_view section_name,
                                std::initializer_list<CardType> allowed) {
        for (const auto& e : list) {
            if (std::find(allowed.begin(), allowed.end(), e.card->type) == allowed.end()) {
                problems.push_back(std::string(section_name) + ": " + e.card->name + " is a " +
                                   std::string(to_string(e.card->type)));
            }
            if (e.card->token) problems.push_back(std::string(section_name) + ": " + e.card->name + " is a token");
        }
    };

    if (total(parsed.legend) != 1) {
        problems.push_back("Legend: needs exactly 1 card, has " + std::to_string(total(parsed.legend)));
    } else if (parsed.legend[0].card->type != CardType::Legend) {
        problems.push_back("Legend: " + parsed.legend[0].card->name + " is not a legend");
    } else {
        deck.legend = parsed.legend[0].card;
    }

    if (total(parsed.champion) != 1) {
        problems.push_back("Champion: needs exactly 1 card (the Chosen Champion), has " +
                           std::to_string(total(parsed.champion)));
    } else if (!parsed.champion[0].card->champion) {
        problems.push_back("Champion: " + parsed.champion[0].card->name + " is not a champion unit (103.3.a.2)");
    } else {
        deck.champion = parsed.champion[0].card;
    }

    wrong_type(parsed.main, "MainDeck", {CardType::Unit, CardType::Spell, CardType::Gear});
    wrong_type(parsed.runes, "Runes", {CardType::Rune});
    wrong_type(parsed.battlefields, "Battlefields", {CardType::Battlefield});
    deck.main = parsed.main;
    deck.runes = parsed.runes;
    for (const auto& e : parsed.battlefields) {
        if (e.count != 1) problems.push_back("Battlefields: " + e.card->name + " more than once (103.5.c)");
        deck.battlefields.push_back(e.card);
    }

    // Counts (103.3–103.5).
    const int main_size = total(parsed.champion) + total(parsed.main);  // also when the champion was refused
    if (main_size < min_main_size) {
        problems.push_back("MainDeck: " + std::to_string(main_size) + " cards with the champion, needs at least " +
                           std::to_string(min_main_size) + " (103.3)");
    }
    if (deck.rune_count() != rune_deck_size) {
        problems.push_back("Runes: " + std::to_string(deck.rune_count()) + " runes, needs exactly " +
                           std::to_string(rune_deck_size) + " (103.4.a)");
    }
    if (static_cast<int>(deck.battlefields.size()) != battlefield_count) {
        problems.push_back("Battlefields: " + std::to_string(deck.battlefields.size()) + ", needs " +
                           std::to_string(battlefield_count) + " for 1v1 (103.5.a)");
    }
    std::map<const Card*, int> copies;  // the chosen champion counts (103.3.b.1)
    if (deck.champion != nullptr) copies[deck.champion] = 1;
    for (const auto& e : deck.main) copies[e.card] += e.count;
    int signatures = 0;
    for (const auto& [card, n] : copies) {
        if (n > max_copies) {
            problems.push_back(card->name + ": " + std::to_string(n) + " copies, at most " + std::to_string(max_copies) +
                               " (103.3.b)");
        }
        if (card->signature) signatures += n;
    }
    if (signatures > max_signatures) {
        problems.push_back(std::to_string(signatures) + " signature cards, at most " + std::to_string(max_signatures) +
                           " (103.3.d.1)");
    }

    // Domain identity and champion tag: need the legend (103.2.b, 103.3.a.2, 103.3.d.2).
    if (deck.legend != nullptr) {
        const Card& legend = *deck.legend;
        const auto shares_tag = [&](const Card& c) {
            return std::any_of(legend.tags.begin(), legend.tags.end(), [&](const auto& t) { return c.has_tag(t); });
        };
        const auto check_identity = [&](const Card& c) {
            if ((c.domains & ~legend.domains) != 0) {
                problems.push_back(c.name + " (" + domains_string(c.domains) + ") is outside the legend's domains " +
                                   domains_string(legend.domains) + " (103.2.b)");
            }
        };
        if (deck.champion != nullptr) {
            check_identity(*deck.champion);
            if (!shares_tag(*deck.champion)) {
                problems.push_back("Champion: " + deck.champion->name + " doesn't have " + legend.name +
                                   "'s champion tag (103.3.a.2)");
            }
        }
        for (const auto& [card, n] : copies) {
            if (card != deck.champion) check_identity(*card);
            if (card->signature && !shares_tag(*card)) {
                problems.push_back(card->name + ": signature card of another champion (103.3.d.2)");
            }
        }
        for (const auto& e : deck.runes) check_identity(*e.card);
        for (const Card* b : deck.battlefields) check_identity(*b);
    }

    if (!problems.empty()) throw DeckError(std::move(problems));
    return deck;
}

Deck load_deck(const CardDb& db, const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("can't open " + path);
    std::stringstream text;
    text << in.rdbuf();
    try {
        return parse_deck(db, text.str());
    } catch (const DeckError& e) {
        auto problems = e.problems();
        for (auto& p : problems) p = path + ": " + p;
        throw DeckError(std::move(problems));
    }
}

} // namespace buddy::riftbound
