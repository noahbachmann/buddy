#include "player.hpp"

#include <algorithm>

namespace buddy::riftbound::ai {

ActionKey key_of(const GameState& s, const Action& a) {
    if (a.obj != no_obj && a.obj < s.objects.size()) {
        const Object& o = s.obj(a.obj);
        if (o.zone == Zone::Hand || o.zone == Zone::Trash) return {a.kind, o.card, no_obj, a.n};
    }
    return {a.kind, nullptr, a.obj, a.n};
}

std::vector<Action> distinct(const GameState& s, const std::vector<Action>& actions) {
    std::vector<Action> out;
    std::vector<ActionKey> keys;
    for (const Action& a : actions) {
        const ActionKey k = key_of(s, a);
        if (std::find(keys.begin(), keys.end(), k) != keys.end()) continue;
        keys.push_back(k);
        out.push_back(a);
    }
    return out;
}

} // namespace buddy::riftbound::ai
