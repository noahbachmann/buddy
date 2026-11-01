// Vi Champion Deck (Unleashed, Fury/Order): decks/vi-unleashed.txt. Each card's text above its rules.
#include "cards.hpp"
#include "common.hpp"

namespace buddy::riftbound::cards {

namespace {

// Rengar, Unseen: "[Accelerate] [Assault 2] [Deflect] [Ganking]"
struct Rengar : CardRules {
    Keywords keywords() const override {
        return cards::keywords(
            {{Keyword::Accelerate, 1}, {Keyword::Assault, 2}, {Keyword::Deflect, 1}, {Keyword::Ganking, 1}});
    }
} const rengar;

// Inferna: "[Ambush] [Assault 2]"
struct Inferna : CardRules {
    Keywords keywords() const override { return cards::keywords({{Keyword::Ambush, 1}, {Keyword::Assault, 2}}); }
} const inferna;

// Sharkling: "[Accelerate] [Assault 4]"
struct Sharkling : CardRules {
    Keywords keywords() const override { return cards::keywords({{Keyword::Accelerate, 1}, {Keyword::Assault, 4}}); }
} const sharkling;

// Towering Pairofant: "[Assault] If a unit died this turn, I enter ready."
struct Pairofant : CardRules {
    Keywords keywords() const override { return cards::keywords({{Keyword::Assault, 1}}); }
    bool enters_ready(const GameState& s, ObjId, const ChainItem&) const override { return s.deaths_this_turn > 0; }
} const pairofant;

// Arena Kingpin: "I enter ready. [E]: Give a unit +3 Might this turn."
struct Kingpin : CardRules {
    static constexpr TargetSpec kTargets[] = {{filters::unit}};
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.cost.exhaust_self = true;
        a.targets = kTargets;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    bool enters_ready(const GameState&, ObjId, const ChainItem&) const override { return true; }
    Status resolve(Resolution& r) const override {
        if (const ObjId t = r.target(); t != no_obj) give_might(r.s, t, 3, r.source(), Duration::Turn);
        return Status::Done;
    }
} const kingpin;

// Loyal Poro: "[Deathknell][>] If I didn't die alone, draw 1. (I wasn't alone if there were other friendly units
// here.)" Alone is noted as it dies (808.1.d.3).
struct LoyalPoro : Deathknell {
    Status resolve(Resolution& r) const override {
        if (!r.item().event.alone) draw(r.s, r.controller(), 1);
        return Status::Done;
    }
} const loyal_poro;

// Carrion Dredger: "[Deathknell][>] Play a 1 Might Bird unit token with [Deflect] to your base."
struct Dredger : Deathknell {
    Status resolve(Resolution& r) const override {
        play_token(r.s, tokens::bird(), r.controller(), played::to_base);
        return Status::Done;
    }
} const dredger;

// Crimson Pigeons: "I have +2 Might while I'm attacking with another unit."
struct Pigeons : CardRules {
    void passive(const GameState& s, ObjId self, ObjId subject, const Traits&, std::vector<Mod>& out) const override {
        const Object& me = s.obj(self);
        if (subject != self || me.designation != Designation::Attacker || me.zone != Zone::Battlefield) return;
        const auto& here = s.battlefields[me.where].here;
        if (std::any_of(here.begin(), here.end(), [&](ObjId id) {
                return id != self && s.obj(id).designation == Designation::Attacker;
            })) {
            out.push_back({ModKind::Might, {}, 2});
        }
    }
} const pigeons;

constexpr Entry kCards[] = {
    {"Rengar, Unseen", &rengar},
    {"Inferna", &inferna},
    {"Sharkling", &sharkling},
    {"Towering Pairofant", &pairofant},
    {"Arena Kingpin", &kingpin},
    {"Loyal Poro", &loyal_poro},
    {"Carrion Dredger", &dredger},
    {"Crimson Pigeons", &pigeons},
};

} // namespace

std::span<const Entry> vi_unleashed() { return kCards; }

} // namespace buddy::riftbound::cards
