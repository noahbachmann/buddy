#include "pay.hpp"

#include <algorithm>
#include <stdexcept>

namespace buddy::riftbound {

namespace {

constexpr Domains all_domains = (1 << domain_count) - 1;

Domains mask(const Cost& cost) { return cost.domains == 0 ? all_domains : cost.domains; }

int rune_domain(const GameState& s, ObjId id) { return domain_index(s.obj(id).card->domains); }

// Power the runes still owe after the pool: `specific` in the cost's domains, `any` of any domain. The pool pays
// like pay() does: domain Power first from matching pool Power, then universal; [A] from what's left.
struct Owed {
    int specific = 0;
    int any = 0;
    int total() const { return specific + any; }
};

Owed owed(const Pool& pool, const Cost& cost) {
    int matching = 0, other = 0;
    for (int d = 0; d < domain_count; ++d) (mask(cost) & (1 << d) ? matching : other) += pool.power[d];
    int power = cost.power;
    const int from_matching = std::min(power, matching);
    power -= from_matching;
    const int from_universal = std::min<int>(power, pool.universal);
    power -= from_universal;
    const int left = matching - from_matching + other + pool.universal - from_universal;
    return {power, std::max(0, cost.any_power - left)};
}

bool matches(const GameState& s, ObjId rune, const Cost& cost) { return s.obj(rune).card->domains & mask(cost); }

bool contains(const std::vector<ObjId>& v, ObjId id) { return std::find(v.begin(), v.end(), id) != v.end(); }

} // namespace

Cost printed_cost(const Card& card) {
    return {card.energy.value_or(0), card.power.value_or(0), card.domains};
}

std::vector<ObjId> runes_of(const GameState& s, PlayerId p) {
    std::vector<ObjId> out;
    for (const ObjId id : s.player(p).base) {
        if (s.obj(id).card->type == CardType::Rune) out.push_back(id);
    }
    return out;
}

int runes_to_recycle(const GameState& s, PlayerId p, const Cost& cost) { return owed(s.player(p).pool, cost).total(); }

bool can_pay(const GameState& s, PlayerId p, const Cost& cost) {
    int ready = 0, matching = 0, all = 0;
    for (const ObjId id : runes_of(s, p)) {
        ready += s.obj(id).exhausted ? 0 : 1;
        matching += matches(s, id, cost) ? 1 : 0;
        ++all;
    }
    const Owed o = owed(s.player(p).pool, cost);
    return ready >= std::max(0, cost.energy - s.player(p).pool.energy) && matching >= o.specific && all >= o.total();
}

std::vector<ObjId> recycle_options(const GameState& s, PlayerId p, const Cost& cost, const std::vector<ObjId>& picked) {
    const Owed o = owed(s.player(p).pool, cost);
    if (static_cast<int>(picked.size()) >= o.total()) return {};
    const int after = o.total() - static_cast<int>(picked.size()) - 1;
    const int from = picked.empty() ? 0 : rune_domain(s, picked.back());
    const int picked_matching =
        static_cast<int>(std::count_if(picked.begin(), picked.end(), [&](ObjId id) { return matches(s, id, cost); }));

    std::vector<ObjId> free;  // usable, not picked, base order; [A] makes every rune usable
    for (const ObjId id : runes_of(s, p)) {
        if ((o.any > 0 || matches(s, id, cost)) && !contains(picked, id)) free.push_back(id);
    }
    std::vector<ObjId> options;
    for (int d = from; d < domain_count; ++d) {
        ObjId best = no_obj;
        int at_or_above = 0, matching_above = 0;  // what later picks can still use
        for (const ObjId id : free) {
            const int rd = rune_domain(s, id);
            if (rd >= d) {
                ++at_or_above;
                matching_above += matches(s, id, cost) ? 1 : 0;
            }
            if (rd != d) continue;
            if (best == no_obj || (s.obj(id).exhausted && !s.obj(best).exhausted)) best = id;
        }
        if (best == no_obj || at_or_above - 1 < after) continue;
        const int m = matches(s, best, cost) ? 1 : 0;  // the domain Power must still be coverable
        if (picked_matching + m + std::min(after, matching_above - m) < o.specific) continue;
        options.push_back(best);
    }
    return options;
}

void pay(GameState& s, PlayerId p, const Cost& cost, const std::vector<ObjId>& recycle_ids) {
    Pool& pool = s.player(p).pool;
    int energy = cost.energy;
    const int from_pool = std::min<int>(energy, pool.energy);
    pool.energy = static_cast<std::int16_t>(pool.energy - from_pool);
    energy -= from_pool;

    const Owed runes = owed(pool, cost);
    const auto take_pool = [&](int power, Domains m) {  // domain Power of `m` first, then universal
        for (int d = 0; d < domain_count && power > 0; ++d) {
            if (!(m & (1 << d))) continue;
            const int take = std::min<int>(power, pool.power[d]);
            pool.power[d] = static_cast<std::int8_t>(pool.power[d] - take);
            power -= take;
        }
        const int universal = std::min<int>(power, pool.universal);
        pool.universal = static_cast<std::int8_t>(pool.universal - universal);
    };
    take_pool(cost.power - runes.specific, mask(cost));
    take_pool(cost.any_power - runes.any, all_domains);

    if (runes.total() != static_cast<int>(recycle_ids.size())) {
        throw std::logic_error("pay: wrong number of runes to recycle");
    }
    int matching = 0;
    for (const ObjId id : recycle_ids) {
        const Object& o = s.obj(id);
        if (o.zone != Zone::Base || o.controller != p || o.card->type != CardType::Rune) {
            throw std::logic_error("pay: can't recycle " + o.card->name);
        }
        matching += matches(s, id, cost) ? 1 : 0;
    }
    if (matching < runes.specific) throw std::logic_error("pay: too few runes of the cost's domains");

    // exhaust for [1] (164.2.a): the runes that get recycled first, they leave anyway
    std::vector<ObjId> order;
    for (const ObjId id : recycle_ids) order.push_back(id);
    for (const ObjId id : runes_of(s, p)) {
        if (!contains(recycle_ids, id)) order.push_back(id);
    }
    for (const ObjId id : order) {
        if (energy == 0) break;
        Object& o = s.obj(id);
        if (o.exhausted) continue;
        o.exhausted = true;
        s.cleanup_due = true;  // 319.7
        --energy;
    }
    if (energy > 0) throw std::logic_error("pay: not enough ready runes");
    recycle(s, recycle_ids);  // 164.2.b: Power of their domains
}

} // namespace buddy::riftbound
