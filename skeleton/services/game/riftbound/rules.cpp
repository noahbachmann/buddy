#include "rules.hpp"

#include <algorithm>
#include <stdexcept>

#include "actions.hpp"
#include "pay.hpp"

namespace buddy::riftbound {

namespace {

const ChainItem* chain_item_of(const GameState& s, ObjId card) {
    for (const ChainItem& c : s.chain) {
        if (c.card == card) return &c;
    }
    return nullptr;
}

bool untargetable(const GameState& s, ObjId id, const ChainItem& by) {
    const CardRules* rules = rules_of(s, id);
    return rules != nullptr && rules->untargetable(s, id, by);
}

Target to_target(const Action& a, std::size_t slot) {
    const auto sl = static_cast<std::uint8_t>(slot);
    if (a.obj == no_obj) return {{}, a.n, sl};
    return {{a.obj, 0}, 0, sl};  // gen filled in by the caller
}

} // namespace

const CardRules* rules_of(const GameState& s, ObjId id) { return s.vanilla ? nullptr : s.obj(id).card->rules; }

const CardRules* rules_of(const GameState& s, const ChainItem& item) {
    return rules_of(s, item.card != no_obj ? item.card : item.source.id);
}

const AbilitySpec* ability_of(const GameState& s, const ChainItem& item) {
    if (item.kind == ItemKind::Card) return nullptr;
    const CardRules* rules = rules_of(s, item);
    if (rules == nullptr) return nullptr;
    const auto all = rules->abilities();
    if (item.ability >= all.size()) throw std::logic_error("chain item with an unknown ability");
    return &all[item.ability];
}

std::span<const TargetSpec> target_specs(const GameState& s, const ChainItem& item) {
    if (item.kind != ItemKind::Card) {
        const AbilitySpec* a = ability_of(s, item);
        return a ? a->targets : std::span<const TargetSpec>{};
    }
    const CardRules* rules = rules_of(s, item);
    return rules ? rules->targets() : std::span<const TargetSpec>{};
}

std::vector<Action> candidates(const GameState& s, const ChainItem& item, std::size_t slot) {
    const TargetSpec& spec = target_specs(s, item)[slot];
    std::vector<Action> out;
    if (spec.players) {
        for (PlayerId p = 0; p < player_count; ++p) {
            if (spec.filter(s, item, {{}, p, static_cast<std::uint8_t>(slot)})) out.push_back({ActionKind::Pick, no_obj, p});
        }
        return out;
    }
    for (ObjId id = 0; id < s.objects.size(); ++id) {
        const Object& o = s.obj(id);
        if (id == item.card) continue;  // 355.9.c
        if (o.zone == Zone::MainDeck || o.zone == Zone::RuneDeck || o.zone == Zone::Removed) continue;  // secret
        if (o.zone == Zone::Chain) {  // 355.9.a: only finalized spells
            const ChainItem* c = chain_item_of(s, id);
            if (c == nullptr || !c->finalized) continue;
        }
        if (!spec.filter(s, item, {o.ref(id), 0, static_cast<std::uint8_t>(slot)})) continue;
        if (!spec.pick && untargetable(s, id, item)) continue;
        out.push_back({ActionKind::Pick, id});
    }
    return out;
}

bool still_valid(const GameState& s, const ChainItem& item, const Target& t) {
    const TargetSpec& spec = target_specs(s, item)[t.slot];
    if (t.ref.id == no_obj) return spec.filter(s, item, t);
    if (!s.alive(t.ref)) return false;  // 359.3.e.4: a new object
    if (s.obj(t.ref.id).zone == Zone::Chain) {
        const ChainItem* c = chain_item_of(s, t.ref.id);
        if (c == nullptr || !c->finalized) return false;
    }
    return spec.filter(s, item, t) && (spec.pick || !untargetable(s, t.ref.id, item));
}

void emit_targeted(GameState& s, const ChainItem& item) {
    const auto specs = target_specs(s, item);
    for (const Target& t : item.targets) {
        if (t.ref.id == no_obj || specs[t.slot].pick) continue;
        emit(s, make_event(EventKind::Targeted, t.ref, item.controller, item.source));
    }
}

Cost total_cost(const GameState& s, const ChainItem& item) {
    const CardRules* rules = rules_of(s, item);
    const bool card = item.kind == ItemKind::Card;
    Cost c;
    if (card) {
        c = printed_cost(*s.obj(item.card).card);
    } else if (const AbilitySpec* a = ability_of(s, item)) {
        c = a->cost;
    }
    if (rules == nullptr) return c;

    std::vector<CostMod> mods;
    const ObjId self = card ? item.card : item.source.id;
    rules->cost_mods(s, self, item, mods);  // 366.2: in any zone it's played from
    for (ObjId id = 0; id < s.objects.size(); ++id) {
        const CardRules* other = rules_of(s, id);
        if (id != self && other != nullptr && other->active_in(s.obj(id).zone)) other->cost_mods(s, id, item, mods);
    }
    for (const CostMod& m : mods) {  // 356.1
        if (m.kind == CostMod::SetBase) {
            c.energy = m.energy;
            c.power = m.power;
        }
    }
    if (card) {  // 356.2
        c = c + rules->additional_cost();
        const auto optional = rules->optional_costs();
        for (std::size_t k = 0; k < optional.size(); ++k) {
            if (item.paid & (1u << k)) c = c + optional[k];
        }
    }
    for (const CostMod& m : mods) {  // 356.3
        if (m.kind == CostMod::Increase) {
            c.energy += m.energy;
            c.power += m.power;
        }
    }
    // 356.4: the player orders discounts; floored ones first never costs more than the other way (356.4.e)
    for (const bool floored : {true, false}) {
        for (const CostMod& m : mods) {
            if (m.kind != CostMod::Discount || (m.min_energy > 0) != floored) continue;
            if (c.energy > m.min_energy) c.energy = std::max(m.min_energy, c.energy - m.energy);
            c.power -= m.power;
        }
    }
    c.energy = std::max(0, c.energy);  // 356.6
    c.power = std::max(0, c.power);
    return c;
}

bool payable(const GameState& s, const ChainItem& item, const Cost& cost) {
    const PlayerId p = item.controller;
    if (!can_pay(s, p, cost)) return false;
    const auto& hand = s.player(p).hand;
    const auto in_hand = static_cast<int>(hand.size()) -
                         (std::find(hand.begin(), hand.end(), item.card) != hand.end() ? 1 : 0);
    if (cost.discard > in_hand) return false;
    if (cost.recycle_trash > s.player(p).trash.size()) return false;
    if (!cost.exhaust_self && !cost.kill_self && !cost.disempower_self) return true;
    if (!s.alive(item.source)) return false;
    const Object& src = s.obj(item.source.id);
    const bool there = on_board(src.zone) || src.zone == Zone::LegendZone;
    if (cost.exhaust_self && (!there || src.exhausted)) return false;
    if (cost.kill_self && !on_board(src.zone)) return false;
    if (cost.disempower_self && !src.empowered) return false;
    return true;
}

// ---- Resolution ----

ChainItem& Resolution::item() {
    for (ChainItem& c : s.chain) {
        if (c.resolving) return c;
    }
    throw std::logic_error("no chain item resolving");
}

const ChainItem& Resolution::item() const { return const_cast<Resolution*>(this)->item(); }

ObjId Resolution::self() const {
    const ObjRef src = item().source;
    return s.alive(src) ? src.id : no_obj;
}

Status Resolution::wait(int next_step) {
    item().step = static_cast<std::uint8_t>(next_step);
    return Status::Wait;
}

std::vector<ObjId> Resolution::targets(std::size_t slot) const {
    std::vector<ObjId> out;
    for (const Target& t : item().targets) {
        if (t.slot == slot && t.ref.id != no_obj && still_valid(s, item(), t)) out.push_back(t.ref.id);
    }
    return out;
}

ObjId Resolution::target(std::size_t slot) const {
    const auto all = targets(slot);
    return all.empty() ? no_obj : all.front();
}

std::optional<PlayerId> Resolution::target_player(std::size_t slot) const {
    for (const Target& t : item().targets) {
        if (t.slot == slot && t.ref.id == no_obj && still_valid(s, item(), t)) return t.player;
    }
    return std::nullopt;
}

std::size_t Resolution::target_count(std::size_t slot) const {
    std::size_t n = 0;
    for (const Target& t : item().targets) {
        if (t.slot == slot && (t.ref.id == no_obj || s.alive(t.ref))) ++n;
    }
    return n;
}

void Resolution::ask(PlayerId player, std::vector<Action> options, int min, int max) {
    max = std::min<int>(max, static_cast<int>(options.size()));
    if (min > max) throw std::logic_error("ask: fewer options than must be picked");
    s.decision = {};
    s.decision.kind = DecisionKind::Choose;
    s.decision.player = player;
    s.decision.purpose = Purpose::Effect;
    s.decision.options = std::move(options);
    s.decision.min = static_cast<std::uint8_t>(min);
    s.decision.max = static_cast<std::uint8_t>(max);
}

std::vector<Action> Resolution::answer() {
    if (!s.answer) throw std::logic_error("resolution: no answer to take");
    auto a = std::move(*s.answer);
    s.answer.reset();
    return a;
}

void Resolution::reflexive(std::uint8_t ability) {
    ChainItem r;
    r.source = item().source;
    r.controller = controller();
    r.kind = ItemKind::Triggered;
    r.ability = ability;
    r.event = item().event;
    s.chain.push_back(r);  // 389.1: pending, finalized once this resolution is done (402.2)
    s.cleanup_due = true;
}

void Resolution::delay(std::uint8_t ability, ObjRef subject, Duration window, bool once) {
    s.delayed.push_back({item().source, ability, controller(), subject, window, once});
}

void Resolution::give(ObjId subject, ModKind kind, int value, Duration duration, Keyword keyword) {
    riftbound::give(s, {.subject = s.obj(subject).ref(subject), .source = item().source, .kind = kind,
                        .keyword = keyword, .value = static_cast<std::int16_t>(value), .duration = duration});
}

Status Resolution::new_choices(ObjId card, bool& negated) {
    negated = false;
    const auto other_index = [&]() -> ChainItem* {
        for (ChainItem& c : s.chain) {
            if (c.card == card && c.finalized && !c.resolving) return &c;
        }
        return nullptr;
    };
    ChainItem* other = other_index();
    if (other == nullptr) {
        negated = true;
        return Status::Done;
    }
    const auto specs = target_specs(s, *other);
    if (item().sub == 0) {
        for (std::size_t k = 0; k < specs.size(); ++k) {
            if (candidates(s, *other, k).size() < specs[k].min) {  // 753.2
                negated = true;
                return Status::Done;
            }
        }
        other->targets.clear();  // 753: every choice again
        item().sub = 1;
    }
    for (;;) {
        const std::size_t slot = item().sub - 1u;
        if (s.answer) {
            for (const Action& a : answer()) {
                Target t = to_target(a, slot);
                if (a.obj != no_obj) t.ref = s.obj(a.obj).ref(a.obj);
                other->targets.push_back(t);
            }
            ++item().sub;
            continue;
        }
        if (slot >= specs.size()) {
            item().sub = 0;
            emit_targeted(s, *other);  // 754
            return Status::Done;
        }
        auto options = candidates(s, *other, slot);
        const int max = std::min<int>(specs[slot].max, static_cast<int>(options.size()));
        if (static_cast<int>(options.size()) == specs[slot].min || max == 0) {
            s.answer = max == 0 ? std::vector<Action>{} : options;  // forced: no decision
            continue;
        }
        ask(controller(), std::move(options), specs[slot].min, max);
        return Status::Wait;
    }
}

namespace filters {

namespace {
const Object* object(const GameState& s, const Target& t) {
    return t.ref.id == no_obj || t.ref.id >= s.objects.size() ? nullptr : &s.obj(t.ref.id);
}
} // namespace

bool unit(const GameState& s, const ChainItem&, const Target& t) {
    const Object* o = object(s, t);
    return o && on_board(o->zone) && o->card->type == CardType::Unit;
}

bool unit_at_battlefield(const GameState& s, const ChainItem& item, const Target& t) {
    return unit(s, item, t) && s.obj(t.ref.id).zone == Zone::Battlefield;
}

bool friendly_unit(const GameState& s, const ChainItem& item, const Target& t) {
    return unit(s, item, t) && s.obj(t.ref.id).controller == item.controller;
}

bool enemy_unit(const GameState& s, const ChainItem& item, const Target& t) {
    return unit(s, item, t) && s.obj(t.ref.id).controller != item.controller;
}

bool spell(const GameState& s, const ChainItem&, const Target& t) {
    const Object* o = object(s, t);
    return o && o->zone == Zone::Chain && o->card->type == CardType::Spell;
}

bool player(const GameState&, const ChainItem&, const Target& t) { return t.ref.id == no_obj; }

} // namespace filters

} // namespace buddy::riftbound
