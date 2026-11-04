#include "tracker.hpp"

#include <algorithm>
#include <utility>

namespace buddy::table {

namespace {

constexpr int max_hand = 100;   // bound for a scan pass
constexpr int max_extras = 4;   // cards left on the spot in a row before giving up

std::vector<std::string> codes_of(const std::vector<Known>& cards) {
    std::vector<std::string> out;
    for (const Known& k : cards) out.push_back(k.code);
    return out;
}

bool same_multiset(std::vector<std::string> a, std::vector<std::string> b) {
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    return a == b;
}

std::vector<CardRef> refs(const std::vector<Known>& cards) {
    std::vector<CardRef> out;
    for (const Known& k : cards) out.push_back({k.id, k.code});
    return out;
}

} // namespace

Scan Tracker::scan(Pile p) {
    ++counts_.scans;
    return backend_.scan(p);
}

Scan Tracker::read(Pile p) {
    std::string last;
    int streak = 0;
    for (int i = 0; i < cfg_.max_reads; ++i) {
        Scan s = scan(p);
        if (s.kind == Scan::Kind::Empty) return s;
        if (s.kind == Scan::Kind::Unreadable) continue;
        streak = s.code == last ? streak + 1 : 1;
        last = s.code;
        if (streak >= cfg_.confirm_reads) return s;
    }
    return {Scan::Kind::Unreadable, {}};
}

void Tracker::place_held(Place to, const Known& card) {
    switch (to) {
        case Place::Hand:
            backend_.place(h_, End::Top);
            belief_.hand.push_back({card.id, card.code, false});
            return;
        case Place::DeckTop:
        case Place::DeckBottom:
            backend_.place(Pile::Deck, to == Place::DeckTop ? End::Top : End::Bottom);
            ++belief_.deck;
            return;
        case Place::RunesBottom:
            backend_.place(Pile::Runes, End::Bottom);
            ++belief_.runes;
            return;
        default:
            backend_.place(to, card.id);
            belief_.spots[static_cast<std::size_t>(to)].push_back({card.id, card.code, false});
    }
}

void Tracker::return_extras(Pile to) {
    if (!cfg_.post_check) return;
    for (int i = 0; i < max_extras; ++i) {
        if (scan(Pile::Spot).kind == Scan::Kind::Empty) return;
        backend_.place(to, End::Top);
    }
    throw Failure(FailReason::Hardware, "the scan spot doesn't empty");
}

void Tracker::merge() {
    backend_.stack(h_, b_);
    std::swap(h_, b_);
}

std::vector<CardRef> Tracker::setup(const std::string& legend, const std::string& champion, int deck, int runes) {
    belief_ = {};
    h_ = Pile::TrayA;
    b_ = Pile::TrayB;
    next_id_ = 1;
    corrections_.clear();
    const int legend_id = next_id_++, champion_id = next_id_++;
    backend_.setup(legend_id, champion_id);
    belief_.spots[static_cast<std::size_t>(Place::Legend)].push_back({legend_id, legend, false});
    belief_.spots[static_cast<std::size_t>(Place::Champion)].push_back({champion_id, champion, false});
    belief_.deck = deck;
    belief_.runes = runes;
    return {{legend_id, legend}, {champion_id, champion}};
}

std::vector<CardRef> Tracker::deal(Source from, Place to, int n) {
    if (belief_.held) throw Failure(FailReason::Invalid, "a peeked card is still on the scan spot");
    const Pile pile = from == Source::Deck ? Pile::Deck : Pile::Runes;
    int& count = from == Source::Deck ? belief_.deck : belief_.runes;
    std::vector<CardRef> out;
    for (int i = 0; i < n; ++i) {
        if (count == 0) throw Failure(FailReason::Invalid, std::string(to_string(from)) + " is empty");
        backend_.deal(pile);
        ++counts_.deals;
        const Scan s = read(Pile::Spot);
        if (s.kind == Scan::Kind::Empty) throw Failure(FailReason::Hardware, "nothing dealt");
        if (s.kind == Scan::Kind::Unreadable) throw Failure(FailReason::Unreadable, "dealt card unreadable");
        --count;
        const Known card{next_id_++, s.code, false};
        place_held(to, card);
        return_extras(pile);  // a double deal: the next card goes back on top
        out.push_back({card.id, card.code});
    }
    return out;
}

CardRef Tracker::peek() {
    if (belief_.held) throw Failure(FailReason::Invalid, "already peeking");
    if (belief_.deck == 0) throw Failure(FailReason::Invalid, "deck is empty");
    backend_.deal(Pile::Deck);
    ++counts_.deals;
    const Scan s = read(Pile::Spot);
    if (s.kind == Scan::Kind::Empty) throw Failure(FailReason::Hardware, "nothing dealt");
    if (s.kind == Scan::Kind::Unreadable) throw Failure(FailReason::Unreadable, "peeked card unreadable");
    --belief_.deck;
    belief_.held = Known{next_id_++, s.code, false};
    return {belief_.held->id, belief_.held->code};
}

void Tracker::peek_done(Place to) {
    if (!belief_.held) throw Failure(FailReason::Invalid, "nothing peeked");
    const Known card = *belief_.held;
    belief_.held.reset();
    place_held(to, card);
    if (!cfg_.post_check) return;
    // A double deal left the deck's second card on the spot. Back on top it goes, unless the peeked card went on top:
    // then it belongs under it, so the peeked card comes back once and both go down in order.
    for (int i = 0; i < max_extras; ++i) {
        if (scan(Pile::Spot).kind == Scan::Kind::Empty) return;
        if (to == Place::DeckTop && i == 0) {
            backend_.deal(Pile::Deck);
            ++counts_.deals;
            backend_.place(Pile::Deck, End::Top);
        }
        backend_.place(Pile::Deck, End::Top);
    }
    throw Failure(FailReason::Hardware, "the scan spot doesn't empty");
}

CardRef Tracker::play(int id, Place to) {
    if (belief_.held) throw Failure(FailReason::Invalid, "a peeked card is still on the scan spot");
    for (int attempt = 0;; ++attempt) {
        auto& hand = belief_.hand;
        const auto it = std::find_if(hand.begin(), hand.end(), [&](const Known& k) { return k.id == id; });
        if (it == hand.end()) {
            find_spot(id);  // Invalid if the table knows the card elsewhere or not at all
            throw Failure(FailReason::Invalid, "card " + std::to_string(id) + " is not in the hand");
        }
        const std::string code = it->code;
        // the nearest copy goes: same code, fewer feeds; it takes the asked id
        const auto k = static_cast<std::size_t>(it - hand.begin());
        const auto j = static_cast<std::size_t>(
            std::find_if(hand.begin(), hand.end(), [&](const Known& c) { return c.code == code; }) - hand.begin());
        std::swap(hand[j].id, hand[k].id);
        for (std::size_t i = 0; i < j; ++i) {
            backend_.feed(h_, b_);
            ++counts_.feeds;
        }
        const Scan s = read(h_);
        if (s.kind == Scan::Kind::Code && s.code == code) {
            backend_.take(h_);
            ++counts_.takes;
            hand.erase(hand.begin() + static_cast<std::ptrdiff_t>(j));
            place_held(to, {id, code, false});
            return_extras(b_);  // a double take: the card above X, whose place in the merged pile is B's top
            merge();
            return {id, code};
        }
        merge();  // the fed cards back under the rest: one pile again
        recover();
        if (attempt == 1) throw Failure(FailReason::NotInHand, "card " + std::to_string(id) + " not found twice");
    }
}

void Tracker::move(int id, Place to) {
    if (std::any_of(belief_.hand.begin(), belief_.hand.end(), [&](const Known& k) { return k.id == id; })) {
        throw Failure(FailReason::Invalid, "card " + std::to_string(id) + " is in the hand: play it");
    }
    const auto [from, i] = find_spot(id);
    auto& list = belief_.spots[static_cast<std::size_t>(from)];
    Known card = list[i];
    list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
    switch (to) {
        case Place::Hand:
            backend_.put(id, h_, End::Top);
            belief_.hand.push_back({id, card.code, false});
            return;
        case Place::DeckTop:
        case Place::DeckBottom:
            backend_.put(id, Pile::Deck, to == Place::DeckTop ? End::Top : End::Bottom);
            ++belief_.deck;
            return;
        case Place::RunesBottom:
            backend_.put(id, Pile::Runes, End::Bottom);
            ++belief_.runes;
            return;
        default:
            backend_.put(id, to);
            card.exhausted = card.exhausted && keeps_exhausted(from, to);
            belief_.spots[static_cast<std::size_t>(to)].push_back(card);
    }
}

void Tracker::exhaust(const std::vector<int>& ids, bool exhausted) {
    for (const int id : ids) find_spot(id);  // all known before anything turns
    for (const int id : ids) {
        const auto [place, i] = find_spot(id);
        backend_.turn(id, exhausted);
        belief_.spots[static_cast<std::size_t>(place)][i].exhausted = exhausted;
    }
}

void Tracker::shuffle_trash_into_deck(const std::vector<int>& order) {
    auto& trash = belief_.spots[static_cast<std::size_t>(Place::Trash)];
    if (!order.empty()) {
        std::vector<int> a = order, b;
        for (const Known& k : trash) b.push_back(k.id);
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        if (a != b) throw Failure(FailReason::Invalid, "order isn't the trash");
    }
    backend_.shuffle_trash_into_deck(order);
    belief_.deck += static_cast<int>(trash.size());
    trash.clear();
}

CardRef Tracker::token(const std::string& code, Place to) {
    if (!is_spot(to)) throw Failure(FailReason::Invalid, "a token goes to a spot");
    const int id = next_id_++;
    backend_.token(to, id, code);
    belief_.spots[static_cast<std::size_t>(to)].push_back({id, code, false});
    return {id, code};
}

void Tracker::remove(int id) {
    const auto [place, i] = find_spot(id);
    backend_.remove(id);
    auto& list = belief_.spots[static_cast<std::size_t>(place)];
    list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
}

void Tracker::rescan_hand() {
    if (belief_.held) throw Failure(FailReason::Invalid, "a peeked card is still on the scan spot");
    recover();
}

std::vector<Event> Tracker::take_corrections() { return std::exchange(corrections_, {}); }

std::pair<Place, std::size_t> Tracker::find_spot(int id) const {
    for (int p = 0; p < place_count; ++p) {
        const auto& list = belief_.spots[static_cast<std::size_t>(p)];
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (list[i].id == id) return {static_cast<Place>(p), i};
        }
    }
    throw Failure(FailReason::Invalid, "unknown card " + std::to_string(id));
}

std::optional<std::vector<std::string>> Tracker::pass() {
    ++counts_.passes;
    std::vector<std::string> codes;
    bool readable = true;
    for (int i = 0; i < max_hand; ++i) {
        const Scan s = read(h_);
        if (s.kind == Scan::Kind::Empty) break;
        if (s.kind == Scan::Kind::Unreadable) readable = false;
        else codes.push_back(s.code);
        backend_.feed(h_, b_);
        ++counts_.feeds;
    }
    std::swap(h_, b_);  // everything was fed: B is the pile now, H empty
    if (!readable) return std::nullopt;
    return codes;
}

bool Tracker::recover() {
    ++counts_.recoveries;
    const auto believed = codes_of(belief_.hand);
    std::optional<std::vector<std::string>> previous;
    for (int i = 0; i < cfg_.max_passes; ++i) {
        const auto got = pass();
        if (!got) continue;
        if (*got == believed) return false;
        // the believed cards in another order (a disturbed pile), or two passes that agree
        if (same_multiset(*got, believed) || (previous && *previous == *got)) {
            adopt(*got);
            return true;
        }
        previous = got;
    }
    throw Failure(FailReason::Hardware, "hand scans don't agree after " + std::to_string(cfg_.max_passes) + " passes");
}

void Tracker::adopt(const std::vector<std::string>& codes) {
    const std::vector<Known> old = belief_.hand;
    std::vector<Known> hand;
    Event e;
    e.kind = EventKind::HandCorrected;
    if (same_multiset(codes, codes_of(old))) {
        // same cards: ids follow their codes, same-code copies in their old order
        std::vector<bool> used(old.size());
        for (const auto& code : codes) {
            for (std::size_t i = 0; i < old.size(); ++i) {
                if (used[i] || old[i].code != code) continue;
                used[i] = true;
                hand.push_back(old[i]);
                break;
            }
        }
    } else {
        // edit-distance alignment: matches keep their ids, substitutions were misread, the rest came or went
        const std::size_t n = old.size(), m = codes.size();
        std::vector<std::vector<int>> d(n + 1, std::vector<int>(m + 1));
        for (std::size_t i = 0; i <= n; ++i) d[i][0] = static_cast<int>(i);
        for (std::size_t j = 0; j <= m; ++j) d[0][j] = static_cast<int>(j);
        for (std::size_t i = 1; i <= n; ++i) {
            for (std::size_t j = 1; j <= m; ++j) {
                const int sub = d[i - 1][j - 1] + (old[i - 1].code == codes[j - 1] ? 0 : 1);
                d[i][j] = std::min({sub, d[i - 1][j] + 1, d[i][j - 1] + 1});
            }
        }
        std::vector<Known> reversed;
        std::size_t i = n, j = m;
        while (i > 0 || j > 0) {
            if (i > 0 && j > 0 && d[i][j] == d[i - 1][j - 1] + (old[i - 1].code == codes[j - 1] ? 0 : 1)) {
                Known k = old[i - 1];
                if (k.code != codes[j - 1]) {
                    k.code = codes[j - 1];
                    e.replaced.push_back({k.id, k.code});
                }
                reversed.push_back(k);
                --i;
                --j;
            } else if (i > 0 && d[i][j] == d[i - 1][j] + 1) {
                e.missing.push_back(old[i - 1].id);
                --i;
            } else {
                reversed.push_back({-1, codes[j - 1], false});  // id once the order is known
                --j;
            }
        }
        hand.assign(reversed.rbegin(), reversed.rend());
        for (Known& k : hand) {
            if (k.id != -1) continue;
            k.id = next_id_++;
            e.extra.push_back({k.id, k.code});
        }
        std::reverse(e.replaced.begin(), e.replaced.end());
        std::reverse(e.missing.begin(), e.missing.end());
    }
    belief_.hand = hand;
    e.hand = refs(hand);
    corrections_.push_back(std::move(e));
}

} // namespace buddy::table
