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
    const CardRules* rules = active_rules(s, id);
    return rules != nullptr && rules->untargetable(s, id, by);
}

Target to_target(const GameState& s, const Action& a, std::size_t slot) {
    const auto sl = static_cast<std::uint8_t>(slot);
    if (a.obj == no_obj) return {{}, a.n, sl};
    return {s.obj(a.obj).ref(a.obj), 0, sl};
}

constexpr AbilitySpec kVision{AbilityKind::Triggered};
constexpr TargetSpec kEquipTarget{filters::friendly_unit};

// One execution's specs (the card's or the ability's).
std::span<const TargetSpec> base_specs(const GameState& s, const ChainItem& item) {
    if (item.kind != ItemKind::Card) {
        const AbilitySpec* a = ability_of(s, item);
        if (a == nullptr) return {};
        if (a->builtin == Builtin::Equip && a->targets.empty()) return {&kEquipTarget, 1};
        return a->targets;
    }
    const CardRules* rules = rules_of(s, item);
    return rules ? rules->targets() : std::span<const TargetSpec>{};
}

bool is_spell(const GameState& s, ObjId id) { return s.obj(id).card->type == CardType::Spell; }

} // namespace

const CardRules* rules_of(const GameState& s, ObjId id) { return s.vanilla ? nullptr : s.obj(id).card->rules; }

const CardRules* active_rules(const GameState& s, ObjId id) {
    const CardRules* rules = rules_of(s, id);
    const Object& o = s.obj(id);
    return rules != nullptr && rules->active_in(o.zone) && o.attached == no_obj ? rules : nullptr;
}

const CardRules* rules_of(const GameState& s, const ChainItem& item) {
    return rules_of(s, item.card != no_obj ? item.card : item.source.id);
}

const AbilitySpec* ability_spec(const GameState& s, ObjId source, std::uint8_t ability) {
    const CardRules* rules = rules_of(s, source);
    if (rules == nullptr) return nullptr;
    if (ability == ability_vision) return &kVision;
    const auto all = rules->abilities();
    if (ability >= all.size()) throw std::logic_error("unknown ability index");
    return &all[ability];
}

const AbilitySpec* ability_of(const GameState& s, const ChainItem& item) {
    if (item.kind == ItemKind::Card) return nullptr;
    return ability_spec(s, item.source.id, item.ability);
}

bool builtin(const GameState& s, const ChainItem& item) {
    if (item.kind == ItemKind::Card) return false;
    const AbilitySpec* a = ability_of(s, item);
    return a != nullptr && (item.ability == ability_vision || a->builtin != Builtin::None);
}

std::size_t target_slots(const GameState& s, const ChainItem& item) {
    return base_specs(s, item).size() * static_cast<std::size_t>(1 + repeats(s, item));
}

const TargetSpec& target_spec(const GameState& s, const ChainItem& item, std::size_t slot) {
    const auto specs = base_specs(s, item);
    return specs[slot % specs.size()];
}

std::vector<OptionalCost> optional_costs(const GameState& s, const ChainItem& item) {
    std::vector<OptionalCost> out;
    const CardRules* rules = item.kind == ItemKind::Card ? rules_of(s, item) : nullptr;
    if (rules == nullptr) return out;
    for (const Cost& c : rules->optional_costs()) out.push_back({c, OptionalCost::Card});
    const Card& card = *s.obj(item.card).card;
    if (card.type == CardType::Unit && has(s, item.card, Keyword::Accelerate)) {  // 805.1.b
        Cost c;
        c.energy = 1;
        c.power = 1;
        c.domains = card.domains;  // colorless: any domain (805.1.b.2)
        out.push_back({c, OptionalCost::Accelerate});
    }
    for (const Cost& c : rules->repeat_costs()) out.push_back({c, OptionalCost::Repeat});
    return out;
}

bool accelerated(const GameState& s, const ChainItem& item) {
    const auto all = optional_costs(s, item);
    for (std::size_t k = 0; k < all.size(); ++k) {
        if (all[k].kind == OptionalCost::Accelerate && (item.paid & (1u << k))) return true;
    }
    return false;
}

int repeats(const GameState& s, const ChainItem& item) {
    if (item.kind != ItemKind::Card || item.paid == 0) return 0;
    const auto all = optional_costs(s, item);
    int n = 0;
    for (std::size_t k = 0; k < all.size(); ++k) {
        if (all[k].kind == OptionalCost::Repeat && (item.paid & (1u << k))) ++n;
    }
    return n;
}

std::vector<Action> candidates(const GameState& s, const ChainItem& item, std::size_t slot) {
    const TargetSpec& spec = target_spec(s, item, slot);
    std::vector<Action> out;
    const auto sl = static_cast<std::uint8_t>(slot);
    if (spec.location) {
        for (std::size_t b = 0; b < s.battlefields.size(); ++b) {
            const ObjId card = s.battlefields[b].card;
            if (spec.filter(s, item, {s.obj(card).ref(card), 0, sl})) {
                out.push_back({ActionKind::Pick, card, static_cast<std::uint8_t>(b)});
            }
        }
        if (spec.filter(s, item, {{}, to_base, sl})) out.push_back({ActionKind::Pick, no_obj, to_base});
        return out;
    }
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
    const TargetSpec& spec = target_spec(s, item, t.slot);
    if (t.ref.id == no_obj) return spec.filter(s, item, t);
    if (!s.alive(t.ref)) return false;  // 359.3.e.4: a new object
    if (s.obj(t.ref.id).zone == Zone::Chain) {
        const ChainItem* c = chain_item_of(s, t.ref.id);
        if (c == nullptr || !c->finalized) return false;
    }
    return spec.filter(s, item, t) && (spec.pick || !untargetable(s, t.ref.id, item));
}

void emit_targeted(GameState& s, const ChainItem& item) {
    for (const Target& t : item.targets) {
        if (t.ref.id == no_obj || target_spec(s, item, t.slot).pick) continue;
        emit(s, make_event(EventKind::Targeted, t.ref, item.controller, item.source));
    }
}

Cost move_tax(const GameState& s, PlayerId p, std::uint8_t where, int units) {
    Cost total;
    if (where == to_base) return total;
    for (ObjId id = 0; id < s.objects.size(); ++id) {
        if (const CardRules* rules = active_rules(s, id)) total = total + rules->move_tax(s, id, p, where, units);
    }
    return total;
}

Cost total_cost(const GameState& s, const ChainItem& item) {
    const CardRules* rules = rules_of(s, item);
    const bool card = item.kind == ItemKind::Card;
    Cost c;
    if (card) {
        c = printed_cost(*s.obj(item.card).card);
        if (rules != nullptr && (item.play & played::flow)) c = rules->flow_cost().value_or(c);  // 829.1.c.1
    } else if (const AbilitySpec* a = ability_of(s, item)) {
        c = a->cost;
    }
    if (rules == nullptr) return c;

    std::vector<CostMod> mods;
    const ObjId self = card ? item.card : item.source.id;
    rules->cost_mods(s, self, item, mods);  // 366.2: in any zone it's played from
    for (ObjId id = 0; id < s.objects.size(); ++id) {
        const CardRules* other = active_rules(s, id);
        if (id != self && other != nullptr) other->cost_mods(s, id, item, mods);
    }
    for (const CostMod& m : mods) {  // 356.1
        if (m.kind == CostMod::SetBase) {
            c.energy = m.energy;
            c.power = m.power;
        }
    }
    if (card) {  // 356.2
        c = c + rules->additional_cost();
        const auto optional = optional_costs(s, item);
        for (std::size_t k = 0; k < optional.size(); ++k) {
            if (item.paid & (1u << k)) c = c + optional[k].cost;
        }
    }
    for (const Target& t : item.targets) {  // 809.1.c: [A] per Deflect for each time an opponent chooses it
        if (t.ref.id == no_obj || !s.alive(t.ref) || target_spec(s, item, t.slot).pick) continue;
        if (!on_board(s.obj(t.ref.id).zone) || s.obj(t.ref.id).controller == item.controller) continue;
        c.any_power += traits(s, t.ref.id).keywords.get(Keyword::Deflect);
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
    c.any_power = std::max(0, c.any_power);
    return c;
}

bool payable(const GameState& s, const ChainItem& item, const Cost& cost) {
    const PlayerId p = item.controller;
    if (!can_pay(s, p, cost)) return false;
    const auto& hand = s.player(p).hand;
    const auto in_hand = std::count_if(hand.begin(), hand.end(), [&](ObjId id) {
        return id != item.card && (!cost.discard_spell || is_spell(s, id));
    });
    if (cost.discard > in_hand) return false;
    if (cost.recycle_trash > s.player(p).trash.size()) return false;
    if (!cost.exhaust_self && !cost.kill_self && !cost.disempower_self && !cost.banish_self) return true;
    if (!s.alive(item.source)) return false;
    const Object& src = s.obj(item.source.id);
    const bool there = on_board(src.zone) || src.zone == Zone::LegendZone;
    if (cost.exhaust_self && (!there || src.exhausted)) return false;
    if (cost.kill_self && !on_board(src.zone)) return false;
    if (cost.disempower_self && !src.empowered) return false;
    return true;
}

bool completable(const GameState& s, const ChainItem& item, std::size_t slot) {
    if (slot >= target_slots(s, item)) return payable(s, item, total_cost(s, item));
    const TargetSpec& spec = target_spec(s, item, slot);
    if (spec.min == 0) return completable(s, item, slot + 1);
    const auto options = candidates(s, item, slot);
    if (options.size() < spec.min) return false;
    if (spec.min > 1) {  // ⚠️ the first ones only: no deck card has such a slot
        ChainItem with = item;
        for (std::size_t k = 0; k < spec.min; ++k) with.targets.push_back(to_target(s, options[k], slot));
        return completable(s, with, slot + 1);
    }
    ChainItem with = item;
    for (const Action& a : options) {
        with.targets = item.targets;
        with.targets.push_back(to_target(s, a, slot));
        if (completable(s, with, slot + 1)) return true;
    }
    return false;
}

bool predict(Resolution& r, PlayerId p) {
    const auto& deck = r.s.player(p).main_deck;
    if (deck.empty()) return false;
    r.s.obj(deck.back()).known |= 1u << p;  // 817.1.b: looks at it
    r.ask(p, {{ActionKind::Pick, deck.back()}}, 0, 1);  // Pick = recycle it
    return true;
}

void predicted(Resolution& r) {
    for (const Action& a : r.answer()) {
        const Object& o = r.s.obj(a.obj);
        if (o.zone == Zone::MainDeck && r.s.player(o.owner).main_deck.back() == a.obj) recycle(r.s, {a.obj});
    }
}

Status resolve_builtin(Resolution& r) {
    if (r.ability() == ability_vision) {  // 817.1.b
        if (r.step() == 0) return predict(r, r.controller()) ? r.wait(1) : Status::Done;
        predicted(r);
        return Status::Done;
    }
    const ObjId self = r.self();
    if (self == no_obj || (!on_board(r.s.obj(self).zone) && r.s.obj(self).zone != Zone::LegendZone)) return Status::Done;
    switch (ability_of(r.s, r.item())->builtin) {
        case Builtin::None: break;
        case Builtin::Empower: empower(r.s, self); break;  // 827.2
        case Builtin::Equip:  // 818.1.b
            if (const ObjId unit = r.target(); unit != no_obj && on_board(r.s.obj(self).zone)) attach(r.s, self, unit);
            break;
    }
    return Status::Done;
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

std::size_t Resolution::slot_of(std::size_t slot) const {
    return slot + item().rep * base_specs(s, item()).size();
}

std::vector<ObjId> Resolution::targets(std::size_t slot) const {
    slot = slot_of(slot);
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
    slot = slot_of(slot);
    for (const Target& t : item().targets) {
        if (t.slot == slot && t.ref.id == no_obj && still_valid(s, item(), t)) return t.player;
    }
    return std::nullopt;
}

std::optional<std::uint8_t> Resolution::location(std::size_t slot) const {
    slot = slot_of(slot);
    for (const Target& t : item().targets) {
        if (t.slot != slot || !still_valid(s, item(), t)) continue;
        return t.ref.id == no_obj ? to_base : s.obj(t.ref.id).where;
    }
    return std::nullopt;
}

std::size_t Resolution::target_count(std::size_t slot) const {
    slot = slot_of(slot);
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
    const std::size_t slots = target_slots(s, *other);
    if (item().sub == 0) {
        for (std::size_t k = 0; k < slots; ++k) {
            if (candidates(s, *other, k).size() < target_spec(s, *other, k).min) {  // 753.2
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
            for (const Action& a : answer()) other->targets.push_back(to_target(s, a, slot));
            ++item().sub;
            continue;
        }
        if (slot >= slots) {
            item().sub = 0;
            emit_targeted(s, *other);  // 754
            return Status::Done;
        }
        auto options = candidates(s, *other, slot);
        const TargetSpec& spec = target_spec(s, *other, slot);
        const int max = std::min<int>(spec.max, static_cast<int>(options.size()));
        if (static_cast<int>(options.size()) == spec.min || max == 0) {
            s.answer = max == 0 ? std::vector<Action>{} : options;  // forced: no decision
            continue;
        }
        ask(controller(), std::move(options), spec.min, max);
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
