#include "sim.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace buddy::table {

namespace {

struct Rate {
    const char* name;
    double SimFaults::*field;
};
constexpr Rate kRates[] = {
    {"misread", &SimFaults::misread},         {"unreadable", &SimFaults::unreadable},
    {"double_feed", &SimFaults::double_feed}, {"missed_feed", &SimFaults::missed_feed},
    {"double_take", &SimFaults::double_take}, {"double_deal", &SimFaults::double_deal},
    {"disturbed", &SimFaults::disturbed},
};

// The 5.5 stress rates (#43) ⚠️: guesses, not measurements.
constexpr SimFaults kStress = {.misread = 0.02, .unreadable = 0.05, .double_feed = 0.02, .missed_feed = 0.02,
                               .double_take = 0.01, .double_deal = 0.01, .disturbed = 0.01};

} // namespace

SimFaults parse_faults(std::string_view text) {
    SimFaults f;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find(',', at);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view item = text.substr(at, end - at);
        at = end + 1;
        if (item.empty() || item == "none") continue;
        if (item == "stress") {
            f = kStress;
            continue;
        }
        const auto eq = item.find('=');
        const std::string_view name = item.substr(0, eq);
        const Rate* rate = std::find_if(std::begin(kRates), std::end(kRates), [&](const Rate& r) { return name == r.name; });
        if (eq == std::string_view::npos || rate == std::end(kRates)) {
            throw std::invalid_argument("unknown fault \"" + std::string(item) + "\"");
        }
        double v = 0;
        try {
            std::size_t used = 0;
            const std::string value(item.substr(eq + 1));
            v = std::stod(value, &used);
            if (used != value.size()) throw std::invalid_argument("");
        } catch (const std::exception&) {
            throw std::invalid_argument("bad rate in \"" + std::string(item) + "\"");
        }
        if (v < 0 || v > 1) throw std::invalid_argument("rate out of 0..1 in \"" + std::string(item) + "\"");
        f.*(rate->field) = v;
    }
    return f;
}

std::string to_string(const SimFaults& f) {
    std::ostringstream out;
    for (const Rate& r : kRates) {
        if (f.*(r.field) == 0) continue;
        if (out.tellp() > 0) out << ',';
        out << r.name << '=' << f.*(r.field);
    }
    return out.tellp() > 0 ? out.str() : "none";
}

Sim::Sim(SimSetup setup, SimFaults faults, std::uint64_t seed) : setup_(std::move(setup)), rates_(faults), rng_(seed) {
    for (const auto* list : {&setup_.deck, &setup_.runes}) codes_.insert(codes_.end(), list->begin(), list->end());
    std::sort(codes_.begin(), codes_.end());
    codes_.erase(std::unique(codes_.begin(), codes_.end()), codes_.end());
}

bool Sim::chance(double p) {
    if (p <= 0) return false;
    return static_cast<double>(rng_() >> 11) * 0x1.0p-53 < p;
}

void Sim::begin_command() {
    placed_.clear();
    // between commands the hand is the one tray that holds cards
    for (const Pile p : {Pile::TrayA, Pile::TrayB}) {
        auto& hand = at(p);
        if (hand.size() < 2 || !chance(rates_.disturbed)) continue;
        const auto i = static_cast<std::size_t>(rng_() % hand.size());
        auto j = static_cast<std::size_t>(rng_() % (hand.size() - 1));
        if (j >= i) ++j;
        std::swap(hand[i], hand[j]);
        ++counts_.disturbed;
    }
}

void Sim::setup(int legend_id, int champion_id) {
    for (auto& p : piles_) p.clear();
    for (auto& s : spots_) s.clear();
    for (const auto& [list, pile] : {std::pair{&setup_.deck, Pile::Deck}, std::pair{&setup_.runes, Pile::Runes}}) {
        for (const auto& code : *list) at(pile).push_back({++serial_, code});
        if (setup_.shuffle) std::shuffle(at(pile).begin(), at(pile).end(), rng_);
    }
    at(Place::Legend).push_back({++serial_, setup_.legend, legend_id});
    at(Place::Champion).push_back({++serial_, setup_.champion, champion_id});
}

Scan Sim::scan(Pile p) {
    const auto& pile = at(p);
    if (pile.empty()) return {Scan::Kind::Empty, {}};
    if (chance(rates_.unreadable)) {
        ++counts_.unreadable;
        return {Scan::Kind::Unreadable, {}};
    }
    const std::string& code = pile.front().code;
    if (codes_.size() > 1 && chance(rates_.misread)) {
        ++counts_.misread;
        std::string wrong;
        do {
            wrong = codes_[rng_() % codes_.size()];
        } while (wrong == code);
        return {Scan::Kind::Code, wrong};
    }
    return {Scan::Kind::Code, code};
}

void Sim::move_bottom(Pile from, Pile to, int n) {
    auto& src = at(from);
    auto& dst = at(to);
    for (int i = 0; i < n && !src.empty(); ++i) {
        dst.push_back(src.front());
        src.erase(src.begin());
    }
}

void Sim::feed(Pile from, Pile to) {
    int n = 1;
    if (chance(rates_.missed_feed)) {
        ++counts_.missed_feed;
        n = 0;
    } else if (at(from).size() > 1 && chance(rates_.double_feed)) {
        ++counts_.double_feed;
        n = 2;
    }
    move_bottom(from, to, n);
}

void Sim::take(Pile from) {
    int n = 1;
    if (at(from).size() > 1 && chance(rates_.double_take)) {
        ++counts_.double_take;
        n = 2;
    }
    move_bottom(from, Pile::Spot, n);
}

void Sim::deal(Pile from) {
    auto& src = at(from);
    int n = 1;
    if (src.size() > 1 && chance(rates_.double_deal)) {
        ++counts_.double_deal;
        n = 2;
    }
    for (int i = 0; i < n && !src.empty(); ++i) {  // the top card lands first: it's the spot's bottom
        at(Pile::Spot).push_back(src.back());
        src.pop_back();
    }
}

void Sim::stack(Pile from, Pile onto) {
    auto& src = at(from);
    auto& dst = at(onto);
    dst.insert(dst.end(), src.begin(), src.end());
    src.clear();
}

void Sim::place(Pile to, End end) {
    auto& spot = at(Pile::Spot);
    if (spot.empty()) throw std::logic_error("place: the scan spot is empty");
    Phys card = spot.front();
    spot.erase(spot.begin());
    if (to != Pile::TrayA && to != Pile::TrayB) placed_.push_back(card.code);
    card.id = 0;
    card.exhausted = false;
    auto& dst = at(to);
    if (end == End::Top) dst.push_back(card);
    else dst.insert(dst.begin(), card);
}

void Sim::place(Place to, int id) {
    auto& spot = at(Pile::Spot);
    if (spot.empty()) throw std::logic_error("place: the scan spot is empty");
    Phys card = spot.front();
    spot.erase(spot.begin());
    placed_.push_back(card.code);
    card.id = id;
    card.exhausted = false;
    at(to).push_back(card);
}

Phys Sim::take_id(int id) {
    for (auto& list : spots_) {
        const auto it = std::find_if(list.begin(), list.end(), [&](const Phys& c) { return c.id == id; });
        if (it == list.end()) continue;
        Phys card = *it;
        list.erase(it);
        return card;
    }
    throw std::logic_error("no card " + std::to_string(id) + " on the table");
}

void Sim::put(int id, Pile to, End end) {
    Phys card = take_id(id);
    card.id = 0;
    card.exhausted = false;
    auto& dst = at(to);
    if (end == End::Top) dst.push_back(card);
    else dst.insert(dst.begin(), card);
}

void Sim::put(int id, Place to) {
    Place from = Place::Base;
    for (int p = 0; p < place_count; ++p) {
        const auto& list = spots_[static_cast<std::size_t>(p)];
        if (std::any_of(list.begin(), list.end(), [&](const Phys& c) { return c.id == id; })) from = static_cast<Place>(p);
    }
    Phys card = take_id(id);
    card.exhausted = card.exhausted && keeps_exhausted(from, to);
    at(to).push_back(card);
}

void Sim::turn(int id, bool exhausted) {
    for (auto& list : spots_) {
        for (Phys& c : list) {
            if (c.id != id) continue;
            c.exhausted = exhausted;
            return;
        }
    }
    throw std::logic_error("no card " + std::to_string(id) + " on the table");
}

void Sim::shuffle_trash_into_deck(const std::vector<int>& order) {
    auto& trash = at(Place::Trash);
    std::vector<Phys> cards;
    if (order.empty()) {
        cards = trash;
        std::shuffle(cards.begin(), cards.end(), rng_);
    } else {
        for (const int id : order) {
            const auto it = std::find_if(trash.begin(), trash.end(), [&](const Phys& c) { return c.id == id; });
            if (it == trash.end()) throw std::logic_error("shuffle: " + std::to_string(id) + " not in the trash");
            cards.push_back(*it);
        }
        if (cards.size() != trash.size()) throw std::logic_error("shuffle: order isn't the whole trash");
    }
    trash.clear();
    auto& deck = at(Pile::Deck);
    for (Phys& c : cards) {
        c.id = 0;
        c.exhausted = false;
    }
    deck.insert(deck.begin(), cards.begin(), cards.end());  // under whatever is left (432: the deck is empty)
}

void Sim::token(Place to, int id, const std::string& code) { at(to).push_back({++serial_, code, id}); }

void Sim::remove(int id) { take_id(id); }

std::string Sim::compare(const Belief& b, Pile hand, bool hand_only) const {
    const Pile other = hand == Pile::TrayA ? Pile::TrayB : Pile::TrayA;
    std::vector<std::string> truth, believed;
    for (const Phys& c : pile(hand)) truth.push_back(c.code);
    for (const Known& k : b.hand) believed.push_back(k.code);
    if (truth != believed) {
        std::string t, s;
        for (const auto& c : truth) t += " " + c;
        for (const auto& c : believed) s += " " + c;
        return "hand is" + t + ", believed" + s;
    }
    if (!pile(other).empty()) return "cards left in the buffer tray";
    const std::size_t held = b.held ? 1 : 0;
    if (pile(Pile::Spot).size() != held) return "scan spot holds " + std::to_string(pile(Pile::Spot).size());
    if (b.held && pile(Pile::Spot).front().code != b.held->code) return "peeked card misread";
    if (hand_only) return {};
    if (static_cast<int>(pile(Pile::Deck).size()) != b.deck) {
        return "deck " + std::to_string(pile(Pile::Deck).size()) + ", believed " + std::to_string(b.deck);
    }
    if (static_cast<int>(pile(Pile::Runes).size()) != b.runes) {
        return "runes " + std::to_string(pile(Pile::Runes).size()) + ", believed " + std::to_string(b.runes);
    }
    for (int p = 0; p < place_count; ++p) {
        const auto place = static_cast<Place>(p);
        std::vector<Known> t, s = b.spots[static_cast<std::size_t>(p)];
        for (const Phys& c : spot(place)) t.push_back({c.id, c.code, c.exhausted});
        const auto by_id = [](const Known& x, const Known& y) { return x.id < y.id; };
        std::sort(t.begin(), t.end(), by_id);
        std::sort(s.begin(), s.end(), by_id);
        if (t != s) return std::string(to_string(place)) + " differs";
    }
    return {};
}

} // namespace buddy::table
