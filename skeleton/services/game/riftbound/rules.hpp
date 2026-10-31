#pragma once
// Card rules: what a card does beyond its stats (360–406). One CardRules object per card (5.3 registers them by name;
// tests define their own), reached through Card::rules. It holds no game state: the engine asks it, and everything it
// creates (chain items, effects, delayed abilities) is plain data in the GameState that points back at it through
// the source card and an ability index. Design: docs/plans/05-riftbound.md → "5.2 design"; how it runs:
// services/game/CLAUDE.md.

#include <optional>
#include <span>
#include <vector>

#include "state.hpp"

namespace buddy::riftbound {

// Whether a candidate is a valid choice for one target slot of `item` (355.9.b). The engine already excludes secret
// zones, pending chain items and the item's own card (355.9.a, 355.9.c) and, for targets, untargetable objects (757).
// A filter checks the rest: type, zone ("unit" = on the board, 355.9.a.1), friendly/enemy relative to
// item.controller, Might, …
using TargetFilter = bool (*)(const GameState& s, const ChainItem& item, const Target& t);

struct TargetSpec {
    TargetFilter filter = nullptr;
    std::uint8_t min = 1, max = 1;  // "up to N": min 0 (355.13)
    bool pick = false;              // "pick" (355.7): not targeted, ignores untargetability, no Targeted event
    bool players = false;           // chooses players instead of objects
};

enum class AbilityKind : std::uint8_t {
    Activated,    // 376: cost ":" effect, offered with priority
    Triggered,    // 382: CardRules::triggers() queues it; also reflexive (387) and delayed (390) triggers
    Replacement,  // 367: CardRules::replace() is asked on every replaceable event
};

// One ability of a card. Static data: a CardRules keeps them in a constexpr array.
struct AbilitySpec {
    AbilityKind kind = AbilityKind::Activated;
    Cost cost{};                           // activated: before ":" (404.1.a); triggered: "[do X] to …" (383.3.b)
    std::span<const TargetSpec> targets{};
    Timing timing = Timing::Normal;        // activated: [Action]/[Reaction] (806, 813); otherwise 381
    bool may = false;                      // triggered: "you may" first, decided at finalization (383.3.a)
    bool add = false;                      // activated [Add]: resolves as soon as it's finalized (401.2)
    std::uint8_t per_turn = 0;             // "once each turn" / "the first time … each turn" (386); 0 = no limit
    bool (*usable)(const GameState& s, ObjId self) = nullptr;  // "use only while …" (377.2.b)
};

// Cost changes (356). SetBase replaces the base cost ("for [cost]", "ignoring its cost", 356.1), Increase and
// Discount change it (356.3, 356.4); a discount's floor applies to that discount only (356.4.e).
struct CostMod {
    enum Kind : std::uint8_t { SetBase, Increase, Discount };
    Kind kind = Discount;
    int energy = 0;
    int power = 0;
    int min_energy = 0;
};

// A passive's modification of one object (363, 479): what it does in which layer, by ModKind.
struct Mod {
    ModKind kind = ModKind::Might;
    Keyword keyword = Keyword::Assault;
    int value = 0;
    bool operator==(const Mod&) const = default;
};

enum class Status : std::uint8_t { Done, Wait };

class Resolution;

class CardRules {
public:
    virtual ~CardRules() = default;

    // Printed keywords (layer base, 479). Their behaviour comes with #25/#26.
    virtual Keywords keywords() const { return {}; }
    // Every ability with an index into this span: activated, triggered (incl. reflexive and delayed), replacement.
    virtual std::span<const AbilitySpec> abilities() const { return {}; }

    // ---- Playing this card (349–359) ----
    virtual std::span<const TargetSpec> targets() const { return {}; }      // 355.5–355.13
    virtual std::span<const Cost> optional_costs() const { return {}; }     // "you may … as an additional cost"
    virtual Cost additional_cost() const { return {}; }                     // mandatory (356.2.a)
    // Restrictions on playing it at all ("play me only …", 366.1); the engine checks type, timing, targets, cost.
    virtual bool can_play(const GameState&, ObjId /*self*/, PlayerId) const { return true; }
    // 369.3: "I enter ready", "If …, I enter ready", Accelerate (the item says which optional costs were paid).
    virtual bool enters_ready(const GameState&, ObjId /*self*/, const ChainItem& /*played*/) const { return false; }

    // ---- Passive abilities (363–366) ----
    // Where this card's passives, triggers and replacements work: the board and the legend zone (365, 384). A card
    // with "… from your trash" text adds that zone.
    virtual bool active_in(Zone zone) const { return on_board(zone) || zone == Zone::LegendZone; }
    // Modifications this card (`self`, active) makes to `subject` (any object, itself included), given the
    // subject's traits so far. Called repeatedly until nothing new applies (478), so conditions may read `now`.
    virtual void passive(const GameState&, ObjId /*self*/, ObjId /*subject*/, const Traits& /*now*/,
                         std::vector<Mod>& /*out*/) const {}
    // Cost modifiers of this card for `item`: its own (any zone it can be played from, 366.2) or another card or
    // ability being played while this one is active.
    virtual void cost_mods(const GameState&, ObjId /*self*/, const ChainItem& /*item*/, std::vector<CostMod>&) const {}
    // 757: "I can't be chosen by …" while `self` is a candidate for `by`.
    virtual bool untargetable(const GameState&, ObjId /*self*/, const ChainItem& /*by*/) const { return false; }

    // ---- Triggered abilities (382–389) ----
    // `e` happened: push the index of every ability whose condition it meets. `self` is active, or the subject of
    // a Die event (its own leave-the-board triggers, 383.2.c.3).
    virtual void triggers(const GameState&, const Event&, ObjId /*self*/, std::vector<std::uint8_t>& /*out*/) const {}
    // A delayed trigger this card created (Delayed::ability) — does `e` meet its condition?
    virtual bool delayed_trigger(const GameState&, const Event&, const Delayed&) const { return false; }

    // ---- Replacement effects (367–375) ----
    // `e` is about to happen. If ability `ability` (static: `delayed` null; delayed: its entry) applies, perform the
    // replacement (its own game actions, or change `e`; set e.cancelled to replace the event entirely) and return
    // true. Each applies at most once per event (370.2).
    virtual bool replace(GameState&, Event& /*e*/, ObjId /*self*/, std::uint8_t /*ability*/,
                         const Delayed* /*delayed*/) const {
        return false;
    }

    // ---- Resolution (359.3.d, 402–406) ----
    // The card's effect (Resolution::ability() == card_effect) or one of its abilities. A step machine: return
    // r.wait(next) after r.ask(…), and the engine calls again with that step and the answer.
    virtual Status resolve(Resolution&) const { return Status::Done; }
};

// A card's rules, nullptr in vanilla mode or for a card without rules.
const CardRules* rules_of(const GameState& s, ObjId id);
// The rules behind a chain item: its card's, or its source's for an ability.
const CardRules* rules_of(const GameState& s, const ChainItem& item);
// The ability a chain item stands for; nullptr for a card.
const AbilitySpec* ability_of(const GameState& s, const ChainItem& item);
std::span<const TargetSpec> target_specs(const GameState& s, const ChainItem& item);

// Valid choices now for slot `slot` of `item`: Pick actions (obj = object, or no_obj + n = player), ascending.
std::vector<Action> candidates(const GameState& s, const ChainItem& item, std::size_t slot);
// Still a legal target as the item resolves (359.3.e.2)? Same object, filter, untargetability.
bool still_valid(const GameState& s, const ChainItem& item, const Target& t);

// 383.4.b: a Targeted event for every chosen (not picked) object of `item`.
void emit_targeted(GameState& s, const ChainItem& item);

// 356: base cost, optional costs paid, cost modifiers of the card itself and of everything active.
Cost total_cost(const GameState& s, const ChainItem& item);
// Runes and pool for Energy/Power, plus the non-standard parts (hand, trash, the source's state).
bool payable(const GameState& s, const ChainItem& item, const Cost& cost);

// The context a CardRules::resolve() works in: the resolving chain item, its targets as they are now, choices and
// the ability helpers. The item stays on the chain while it resolves (359.3.e.15).
class Resolution {
public:
    explicit Resolution(GameState& s) : s(s) {}
    GameState& s;

    ChainItem& item();
    const ChainItem& item() const;
    PlayerId controller() const { return item().controller; }
    std::uint8_t ability() const { return item().ability; }
    // The source while it is still the same object (abilities), else no_obj (359.3.e.12: null).
    ObjId self() const;
    int step() const { return item().step; }
    Status wait(int next_step);

    // Targets of `slot` that are still legal (359.3.e.5: the others are unaffected).
    std::vector<ObjId> targets(std::size_t slot = 0) const;
    // The first one, or no_obj when it mistargeted (its instructions are ignored).
    ObjId target(std::size_t slot = 0) const;
    std::optional<PlayerId> target_player(std::size_t slot = 0) const;
    // How many targets the slot had (359.3.e.9.a: mistargeted ones on the board count, ones gone off the board don't).
    std::size_t target_count(std::size_t slot = 0) const;

    // Opens a Choose decision (355.18). The answer arrives with the next call: answer().
    void ask(PlayerId player, std::vector<Action> options, int min, int max);
    std::vector<Action> answer();

    // Attribution of game actions this resolution performs (429.5.b).
    ObjRef source() const { return item().source; }

    // 389: a reflexive trigger of the same source goes on the chain as a pending item (after this resolution).
    void reflexive(std::uint8_t ability);
    // 390: a delayed ability of the same source. Its window ends with `window` (Turn: this turn).
    void delay(std::uint8_t ability, ObjRef subject = {}, Duration window = Duration::Turn, bool once = true);
    // A continuous effect from this item on `subject` (snapshots are the caller's: actions.hpp give_might).
    void give(ObjId subject, ModKind kind, int value, Duration duration = Duration::Turn,
              Keyword keyword = Keyword::Assault);
    std::int16_t& var(std::size_t i) { return item().vars.at(i); }

    // 750: the controller makes new choices for the finalized chain item of card `card` (all targets again, legal
    // ones only, costs ignored, Targeted events again). Returns Wait while choosing; `negated` when no legal set of
    // choices exists (753.2). Uses item().sub for its progress.
    Status new_choices(ObjId card, bool& negated);
};

// Common target filters (355.9.a.1: "unit" = on the board).
namespace filters {
bool unit(const GameState& s, const ChainItem& item, const Target& t);
bool unit_at_battlefield(const GameState& s, const ChainItem& item, const Target& t);
bool friendly_unit(const GameState& s, const ChainItem& item, const Target& t);
bool enemy_unit(const GameState& s, const ChainItem& item, const Target& t);
bool spell(const GameState& s, const ChainItem& item, const Target& t);  // a finalized spell on the chain
bool player(const GameState& s, const ChainItem& item, const Target& t);
} // namespace filters

} // namespace buddy::riftbound
