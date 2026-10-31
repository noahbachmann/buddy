#include "tokens.hpp"

#include <algorithm>

#include "actions.hpp"
#include "rules.hpp"

namespace buddy::riftbound::tokens {

namespace {

Card token(std::string name, CardType type, int might, const CardRules& rules) {
    Card c;
    c.name = std::move(name);
    c.type = type;
    c.token = true;
    if (type == CardType::Unit) c.might = might;  // 185.3: no cost, no domain
    c.rules = &rules;
    return c;
}

struct Bird : CardRules {
    Keywords keywords() const override {
        Keywords k;
        k.grant(Keyword::Deflect);
        return k;
    }
} const bird_rules;

struct ShadowClone : CardRules {
    static constexpr AbilitySpec kAbilities[] = {{AbilityKind::Triggered}};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState&, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (e.kind == EventKind::Attack && e.subject.id == self) out.push_back(0);
    }
    Status resolve(Resolution& r) const override {
        const PlayerId p = r.controller();
        if (r.step() == 0) {  // "you may banish a unit from your trash": a pick, not a target
            std::vector<Action> units;
            for (const ObjId id : r.s.player(p).trash) {
                if (r.s.obj(id).card->type == CardType::Unit) units.push_back({ActionKind::Pick, id});
            }
            std::sort(units.begin(), units.end(), [](const Action& a, const Action& b) { return a.obj < b.obj; });
            if (units.empty()) return Status::Done;
            r.ask(p, std::move(units), 0, 1);
            return r.wait(1);
        }
        const auto picked = r.answer();
        if (picked.empty()) return Status::Done;
        banish(r.s, picked.front().obj, {r.source(), p});
        if (const ObjId me = r.self(); me != no_obj) r.give(me, ModKind::Grant, 4, Duration::Turn, Keyword::Assault);
        return Status::Done;
    }
} const shadow_clone_rules;

struct Gold : CardRules {
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.cost.kill_self = a.cost.exhaust_self = true;
        a.timing = Timing::Reaction;
        a.add = true;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    Status resolve(Resolution& r) const override {
        add(r.s, r.controller(), 0, 1);
        return Status::Done;
    }
} const gold_rules;

} // namespace

const Card& bird() {
    static const Card c = [] {
        Card t = token("Bird", CardType::Unit, 1, bird_rules);
        t.tags = {"Bird"};
        return t;
    }();
    return c;
}

const Card& shadow_clone() {
    static const Card c = token("Shadow Clone", CardType::Unit, 0, shadow_clone_rules);
    return c;
}

const Card& gold() {
    static const Card c = token("Gold", CardType::Gear, 0, gold_rules);
    return c;
}

} // namespace buddy::riftbound::tokens
