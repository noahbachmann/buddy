#include "pay.hpp"

#include <algorithm>
#include <stdexcept>

namespace buddy::riftbound {

namespace {

constexpr Domains all_domains = (1 << domain_count) - 1;

Domains mask(const Cost& cost) { return cost.domains == 0 ? all_domains : cost.domains; }

int rune_domain(const GameState& s, ObjId id) { return domain_index(s.obj(id).card->domains); }

// Pool Power that pays this cost.
int pool_power(const Pool& pool, Domains m) {
    int sum = pool.universal;
    for (int d = 0; d < domain_count; ++d) {
        if (m & (1 << d)) sum += pool.power[d];
    }
    return sum;
}

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

int runes_to_recycle(const GameState& s, PlayerId p, const Cost& cost) {
    return std::max(0, cost.power - pool_power(s.player(p).pool, mask(cost)));
}

bool can_pay(const GameState& s, PlayerId p, const Cost& cost) {
    int ready = 0, matching = 0;
    for (const ObjId id : runes_of(s, p)) {
        ready += s.obj(id).exhausted ? 0 : 1;
        matching += (s.obj(id).card->domains & mask(cost)) ? 1 : 0;
    }
    return ready >= std::max(0, cost.energy - s.player(p).pool.energy) && matching >= runes_to_recycle(s, p, cost);
}

std::vector<ObjId> recycle_options(const GameState& s, PlayerId p, const Cost& cost, const std::vector<ObjId>& picked) {
    const int owed = runes_to_recycle(s, p, cost);
    if (static_cast<int>(picked.size()) >= owed) return {};
    const int after = owed - static_cast<int>(picked.size()) - 1;
    const int from = picked.empty() ? 0 : rune_domain(s, picked.back());

    std::vector<ObjId> free;  // matching, not picked, base order
    for (const ObjId id : runes_of(s, p)) {
        if ((s.obj(id).card->domains & mask(cost)) && !contains(picked, id)) free.push_back(id);
    }
    std::vector<ObjId> options;
    for (int d = from; d < domain_count; ++d) {
        ObjId best = no_obj;
        int at_or_above = 0;  // what later picks can still use
        for (const ObjId id : free) {
            const int rd = rune_domain(s, id);
            if (rd >= d) ++at_or_above;
            if (rd != d) continue;
            if (best == no_obj || (s.obj(id).exhausted && !s.obj(best).exhausted)) best = id;
        }
        if (best != no_obj && at_or_above - 1 >= after) options.push_back(best);
    }
    return options;
}

void pay(GameState& s, PlayerId p, const Cost& cost, const std::vector<ObjId>& recycle_ids) {
    Pool& pool = s.player(p).pool;
    int energy = cost.energy;
    const int from_pool = std::min<int>(energy, pool.energy);
    pool.energy = static_cast<std::int16_t>(pool.energy - from_pool);
    energy -= from_pool;

    int power = cost.power;
    for (int d = 0; d < domain_count && power > 0; ++d) {
        if (!(mask(cost) & (1 << d))) continue;
        const int take = std::min<int>(power, pool.power[d]);
        pool.power[d] = static_cast<std::int8_t>(pool.power[d] - take);
        power -= take;
    }
    const int universal = std::min<int>(power, pool.universal);
    pool.universal = static_cast<std::int8_t>(pool.universal - universal);
    power -= universal;

    if (power != static_cast<int>(recycle_ids.size())) throw std::logic_error("pay: wrong number of runes to recycle");
    for (const ObjId id : recycle_ids) {
        const Object& o = s.obj(id);
        if (o.zone != Zone::Base || o.controller != p || !(o.card->domains & mask(cost))) {
            throw std::logic_error("pay: can't recycle " + o.card->name);
        }
    }

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
