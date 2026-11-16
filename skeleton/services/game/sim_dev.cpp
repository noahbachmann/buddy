#include "sim_dev.hpp"

#include <algorithm>
#include <stdexcept>

#include "riftbound/engine.hpp"
#include "riftbound/hidden.hpp"

namespace buddy::game {

using riftbound::Action;
using riftbound::ActionKind;
using riftbound::Card;
using riftbound::CardType;
using riftbound::GameState;
using riftbound::Zone;

SimDev::SimDev(PlayerId seat, std::unique_ptr<riftbound::ai::Player> player, std::uint64_t seed, bool shuffle)
    : seat_(seat), player_(std::move(player)), rng_(seed), shuffle_(shuffle) {}

void SimDev::begin(const GameState& s, const riftbound::Deck& seat0, const riftbound::Deck& seat1) {
    player_->begin(seat_, seat0, seat1);
    truth_.assign(s.objects.size(), nullptr);
    const auto buddy = static_cast<std::uint8_t>(1u << riftbound::opponent(seat_));
    std::vector<ObjId> main, runes, fields;
    for (ObjId id = 0; id < s.objects.size(); ++id) {
        const auto& o = s.obj(id);
        if (o.owner != seat_) continue;
        truth_[id] = o.card;
        if (o.card->type == CardType::Battlefield) fields.push_back(id);
        else if (!(o.known & buddy)) (o.card->type == CardType::Rune ? runes : main).push_back(id);
    }
    if (!shuffle_) return;
    for (auto* ids : {&main, &runes, &fields}) {  // a permutation of the cards over the same objects
        std::vector<const Card*> cards;
        for (const ObjId id : *ids) cards.push_back(truth_[id]);
        rng_.shuffle(cards);
        for (std::size_t i = 0; i < ids->size(); ++i) truth_[(*ids)[i]] = cards[i];
    }
}

bool SimDev::hidden_hand(const GameState& s, ObjId obj) const {
    if (obj == riftbound::no_obj || obj >= s.objects.size()) return false;
    const auto& o = s.obj(obj);
    return o.owner == seat_ && o.zone == Zone::Hand && !(o.known & 1u << riftbound::opponent(seat_));
}

GameState SimDev::true_state(const GameState& s) const {
    GameState t = s;
    const auto buddy = static_cast<std::uint8_t>(1u << riftbound::opponent(seat_));
    for (ObjId id = 0; id < truth_.size() && id < t.objects.size(); ++id) {
        if (truth_[id] && !(t.obj(id).known & buddy)) t.obj(id).card = truth_[id];
    }
    return t;
}

std::vector<const Card*> SimDev::reveal(const DevPrompt& p) const {
    std::vector<const Card*> cards;
    for (const ObjId id : p.objs) cards.push_back(truth(id));
    return cards;
}

// An unnamed option assumes `o.card` on `o.obj`: put a true card there that allows the action (some hidden hand card
// of Dev's does, or SimDev's player wouldn't have picked it), so a later prompt for that object can answer truly.
void SimDev::relabel(const GameState& s, const DevOption& o) {
    for (const ObjId x : s.player(seat_).hand) {
        if (!hidden_hand(s, x)) continue;
        GameState copy = s;
        if (!riftbound::bind(copy, o.obj, truth_[x], riftbound::opponent(seat_))) continue;
        const auto legal = riftbound::legal_actions(copy);
        if (std::find(legal.begin(), legal.end(), o.action) == legal.end()) continue;
        std::swap(truth_[x], truth_[o.obj]);
        return;
    }
}

DevOption SimDev::choose(const GameState& s, const std::vector<DevOption>& options) {
    const GameState t = true_state(s);
    const auto actions = riftbound::legal_actions(t);
    const Action a = actions.size() == 1 ? actions[0] : player_->choose(riftbound::view(t, seat_), actions);
    const auto fail = [&] {
        return std::logic_error("SimDev: " + riftbound::describe(t, a) + " isn't among Dev's " +
                                std::to_string(options.size()) + " options");
    };
    if (!hidden_hand(s, a.obj)) {
        const DevOption* unnamed = nullptr;
        for (const auto& o : options) {
            if (o.action != a) continue;
            if (o.named && !o.card) return o;
            if (!o.named) unnamed = &o;
        }
        if (!unnamed) throw fail();
        relabel(s, *unnamed);
        return *unnamed;
    }
    if (s.decision.kind == riftbound::DecisionKind::Mulligan) {  // by count: whichever hidden card the option names
        for (const auto& o : options) {
            if (o.action.kind == ActionKind::Pick && hidden_hand(s, o.action.obj)) {
                std::swap(truth_[a.obj], truth_[o.action.obj]);
                return o;
            }
        }
        throw fail();
    }
    const auto same_kind = [&](const DevOption& o) { return o.named && o.card && o.action.kind == a.kind && o.action.n == a.n; };
    for (const auto& o : options) {
        if (same_kind(o) && o.card == truth_[a.obj]) {
            std::swap(truth_[a.obj], truth_[o.obj]);
            return o;
        }
    }
    // A Choose whose options the placeholders filtered ("discard a spell"): another true hidden hand card that fits.
    for (const auto& o : options) {
        if (!same_kind(o)) continue;
        for (const ObjId x : s.player(seat_).hand) {
            if (!hidden_hand(s, x) || truth_[x] != o.card) continue;
            std::swap(truth_[x], truth_[o.obj]);
            return o;
        }
    }
    throw fail();
}

} // namespace buddy::game
