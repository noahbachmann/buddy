#include "eval.hpp"

#include <algorithm>
#include <limits>

namespace buddy::riftbound::ai {

namespace {

double side(const GameState& s, PlayerId p, const Weights& w) {
    const Player& pl = s.player(p);
    const int past = std::max(0, pl.points - (victory_score - 3));
    double v = w.point * pl.points + w.near_win * past * past;
    for (const Battlefield& b : s.battlefields) v += b.controller == p ? w.control : 0;
    v += w.hand * static_cast<double>(pl.hand.size());
    for (ObjId id = 0; id < s.objects.size(); ++id) {
        const Object& o = s.obj(id);
        if (o.zone == Zone::LegendZone && o.owner == p) v += o.exhausted ? 0 : w.legend_ready;
        if (!on_board(o.zone) || o.controller != p) continue;
        switch (o.card->type) {
            case CardType::Unit: {
                const int m = might(s, id);
                v += w.unit + w.might * m + (o.zone == Zone::Battlefield ? w.might_field * m : 0);
                break;
            }
            case CardType::Gear: v += w.gear; break;
            case CardType::Rune: v += w.rune; break;
            default: break;
        }
    }
    return v;
}

} // namespace

double eval(const GameState& s, PlayerId p, const Weights& w) {
    constexpr double inf = std::numeric_limits<double>::infinity();
    if (s.winner) return *s.winner == p ? inf : -inf;
    return side(s, p, w) - side(s, opponent(p), w);
}

} // namespace buddy::riftbound::ai
