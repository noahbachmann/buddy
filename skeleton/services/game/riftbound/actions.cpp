#include "actions.hpp"

#include <algorithm>
#include <stdexcept>

#include "rules.hpp"

namespace buddy::riftbound {

namespace {

// A passive's modification as applied in one evaluation: the same mod from the same source at the same position
// counts once (478.2: every effect applies a single time).
struct Applied {
    ObjId source;
    std::uint8_t ordinal;
    Mod mod;
    bool operator==(const Applied&) const = default;
};

// 478.1.a: a passive that reads another object's traits evaluates that object first; this bounds the recursion.
thread_local int layer_depth = 0;
constexpr int max_layer_depth = 4;

Traits apply_layers(const Object& o, const Keywords& printed, const std::vector<Applied>& passives,
                    const std::vector<const Effect*>& effects) {
    Traits t;
    t.might = o.card->might.value_or(0);
    t.keywords = printed;
    const auto each = [&](auto&& f) {  // passives first, then effects by timestamp (482)
        for (const Applied& a : passives) f(a.mod.kind, a.mod.keyword, a.mod.value);
        for (const Effect* e : effects) f(e->kind, e->keyword, e->value);
    };
    each([&](ModKind k, Keyword, int v) {  // layer 2: base might
        if (k == ModKind::BaseMight) t.might = v;
    });
    each([&](ModKind k, Keyword kw, int v) {  // layer 3: keywords
        if (k == ModKind::Grant) t.keywords.grant(kw, v);
        if (k == ModKind::Lose) t.keywords.lose(kw);
    });
    t.might += o.buffs;  // layer 4 (479.4.e): increases, then "increased to", then decreases
    each([&](ModKind k, Keyword, int v) {
        if (k == ModKind::Might && v > 0) t.might += v;
    });
    each([&](ModKind k, Keyword, int v) {
        if (k == ModKind::RaiseMight) t.might = std::max(t.might, v);
    });
    each([&](ModKind k, Keyword, int v) {
        if (k == ModKind::Might && v < 0) t.might += v;
    });
    return t;
}

bool same_place(const Object& a, const Object& b) {
    if (a.zone != b.zone || (a.controller != b.controller && a.zone == Zone::Base)) return false;
    return a.zone != Zone::Battlefield || a.where == b.where;
}

// Loyal Poro's "alone" (808): no other friendly unit at its location.
bool alone(const GameState& s, ObjId id) {
    const Object& o = s.obj(id);
    for (ObjId other = 0; other < s.objects.size(); ++other) {
        const Object& x = s.obj(other);
        if (other != id && on_board(x.zone) && x.card->type == CardType::Unit && x.controller == o.controller &&
            same_place(o, x)) {
            return false;
        }
    }
    return true;
}

const AbilitySpec* spec_at(const CardRules* rules, std::uint8_t ability) {
    if (rules == nullptr) return nullptr;
    const auto all = rules->abilities();
    if (ability >= all.size()) throw std::logic_error("ability index out of range");
    return &all[ability];
}

void queue(GameState& s, ObjRef source, std::uint8_t ability, PlayerId controller, const Event& e,
           const AbilitySpec& spec) {
    if (spec.per_turn > 0) {  // 383.3.e
        if (uses(s, source, ability) >= spec.per_turn) return;
        use(s, source, ability);
    }
    s.triggered.push_back({source, ability, controller, e});
}

} // namespace

Traits traits(const GameState& s, ObjId id) {
    const Object& o = s.obj(id);
    const CardRules* own = rules_of(s, id);
    const Keywords printed = own ? own->keywords() : Keywords{};
    std::vector<const Effect*> effects;
    for (const Effect& e : s.effects) {
        if (e.subject == o.ref(id)) effects.push_back(&e);
    }
    std::vector<Applied> applied;
    Traits t = apply_layers(o, printed, applied, effects);
    if (s.vanilla || layer_depth >= max_layer_depth) return t;

    ++layer_depth;
    struct Guard {
        ~Guard() { --layer_depth; }
    } guard;
    std::vector<Mod> out;
    for (int round = 0; round < 8; ++round) {  // 478.3: again until nothing new applies
        bool added = false;
        for (ObjId src = 0; src < s.objects.size(); ++src) {
            const CardRules* rules = rules_of(s, src);
            if (rules == nullptr || !rules->active_in(s.obj(src).zone)) continue;
            out.clear();
            rules->passive(s, src, id, t, out);
            for (std::size_t k = 0; k < out.size(); ++k) {
                const Applied a{src, static_cast<std::uint8_t>(k), out[k]};
                if (std::find(applied.begin(), applied.end(), a) == applied.end()) {
                    applied.push_back(a);
                    added = true;
                }
            }
        }
        if (!added) break;
        t = apply_layers(o, printed, applied, effects);
    }
    return t;
}

int might(const GameState& s, ObjId id) { return traits(s, id).might; }

bool has(const GameState& s, ObjId id, Keyword k) { return traits(s, id).keywords.has(k); }

bool mighty(const GameState& s, ObjId id) { return might(s, id) >= 5; }

Place place_of(const GameState& s, ObjId id) {
    const Object& o = s.obj(id);
    return {o.zone, o.where, o.controller};
}

PlayerId controller_of(const GameState& s, ObjId id) {
    const Object& o = s.obj(id);
    return on_board(o.zone) ? o.controller : o.owner;
}

void emit(GameState& s, const Event& e) {
    if (s.vanilla) return;
    std::vector<std::uint8_t> out;
    for (ObjId id = 0; id < s.objects.size(); ++id) {
        const CardRules* rules = rules_of(s, id);
        if (rules == nullptr) continue;
        if (e.kind == EventKind::Die ? id != e.subject.id : !rules->active_in(s.obj(id).zone)) continue;
        out.clear();
        rules->triggers(s, e, id, out);
        const ObjRef self = e.kind == EventKind::Die ? e.subject : s.obj(id).ref(id);
        for (const std::uint8_t a : out) queue(s, self, a, controller_of(s, id), e, *spec_at(rules, a));
    }
    for (std::size_t i = 0; i < s.delayed.size();) {
        const Delayed d = s.delayed[i];
        const CardRules* rules = s.obj(d.source.id).card->rules;
        const AbilitySpec* spec = spec_at(rules, d.ability);
        if (spec != nullptr && spec->kind == AbilityKind::Triggered && rules->delayed_trigger(s, e, d)) {
            queue(s, d.source, d.ability, d.controller, e, *spec);
            if (d.once) {
                s.delayed.erase(s.delayed.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
        }
        ++i;
    }
}

bool replaced(GameState& s, Event& e) {
    if (s.vanilla) return false;
    struct Key {
        ObjRef source;
        std::uint8_t ability;
        bool delayed;
        bool operator==(const Key&) const = default;
    };
    std::vector<Key> done;  // 370.2
    const auto fresh = [&](const Key& k, const AbilitySpec& spec) {
        return std::find(done.begin(), done.end(), k) == done.end() &&
               (spec.per_turn == 0 || uses(s, k.source, k.ability) < spec.per_turn);
    };
    while (!e.cancelled) {
        bool applied = false;
        for (std::size_t i = s.delayed.size(); i-- > 0 && !applied;) {
            const Delayed d = s.delayed[i];
            const CardRules* rules = s.obj(d.source.id).card->rules;
            const AbilitySpec* spec = spec_at(rules, d.ability);
            const Key key{d.source, d.ability, true};
            if (spec == nullptr || spec->kind != AbilityKind::Replacement || !fresh(key, *spec)) continue;
            if (!rules->replace(s, e, d.source.id, d.ability, &d)) continue;
            done.push_back(key);
            if (spec->per_turn > 0) use(s, d.source, d.ability);
            if (d.once) {  // the replacement's own actions may have changed the list
                const auto it = std::find(s.delayed.begin(), s.delayed.end(), d);
                if (it != s.delayed.end()) s.delayed.erase(it);
            }
            applied = true;
        }
        for (ObjId id = 0; id < s.objects.size() && !applied; ++id) {
            const CardRules* rules = rules_of(s, id);
            if (rules == nullptr || !rules->active_in(s.obj(id).zone)) continue;
            const auto all = rules->abilities();
            for (std::size_t a = 0; a < all.size() && !applied; ++a) {
                const Key key{s.obj(id).ref(id), static_cast<std::uint8_t>(a), false};
                if (all[a].kind != AbilityKind::Replacement || !fresh(key, all[a])) continue;
                if (!rules->replace(s, e, id, key.ability, nullptr)) continue;
                done.push_back(key);
                if (all[a].per_turn > 0) use(s, key.source, key.ability);
                applied = true;
            }
        }
        if (!applied) break;
    }
    return e.cancelled;
}

bool kill(GameState& s, ObjId id, const Cause& cause) { return !kill_all(s, {id}, cause).empty(); }

std::vector<ObjId> kill_all(GameState& s, const std::vector<ObjId>& ids, const Cause& cause) {
    std::vector<Event> events;
    for (const ObjId id : ids) {
        const Object& o = s.obj(id);
        if (!on_board(o.zone)) continue;  // 429.2.a
        Event e = make_event(EventKind::Die, o.ref(id), o.controller, cause.source);
        e.place = place_of(s, id);
        e.alone = alone(s, id);
        events.push_back(e);
    }
    std::vector<Event> dying;
    for (Event& e : events) {
        // an earlier event's replacement may have moved it already
        if (!s.alive(e.subject) || !on_board(s.obj(e.subject.id).zone)) continue;
        if (!replaced(s, e)) dying.push_back(e);
    }
    for (const Event& e : dying) emit(s, e);  // 383.2.c.3: own death triggers while still on the board
    std::vector<Event> died;
    for (Event e : dying) {
        if (!s.alive(e.subject) || !on_board(s.obj(e.subject.id).zone)) continue;
        const Object& o = s.obj(e.subject.id);
        if (o.card->type == CardType::Unit && s.deaths_this_turn < 255) ++s.deaths_this_turn;
        move_to(s, e.subject.id, Zone::Trash, o.owner);
        e.kind = EventKind::Died;
        died.push_back(e);
    }
    std::vector<ObjId> out;
    for (const Event& e : died) {
        emit(s, e);
        out.push_back(e.subject.id);
    }
    return out;
}

int deal(GameState& s, ObjId id, int amount, const Cause& cause) {
    Object& o = s.obj(id);
    if (amount <= 0 || !on_board(o.zone)) return 0;
    Event e = make_event(EventKind::Damage, o.ref(id), cause.player, cause.source);
    e.place = place_of(s, id);
    e.amount = static_cast<std::int16_t>(amount);
    if (replaced(s, e) || e.amount <= 0) return 0;
    s.obj(id).damage = static_cast<std::int16_t>(s.obj(id).damage + e.amount);
    s.cleanup_due = true;
    emit(s, e);
    return e.amount;
}

void heal(GameState& s, ObjId id) {
    s.obj(id).damage = 0;
    s.cleanup_due = true;
}

void exhaust(GameState& s, ObjId id) {
    s.obj(id).exhausted = true;
    s.cleanup_due = true;
}

void ready(GameState& s, ObjId id) {
    s.obj(id).exhausted = false;
    s.cleanup_due = true;
}

void recall(GameState& s, ObjId id) {
    const Object& o = s.obj(id);
    if (!on_board(o.zone) || o.zone == Zone::Base) return;
    move_to(s, id, Zone::Base, o.controller);
    s.cleanup_due = true;
}

void stun(GameState& s, ObjId id) {
    s.obj(id).stunned = true;
    s.cleanup_due = true;
}

bool buff(GameState& s, ObjId id) {
    Object& o = s.obj(id);
    if (o.buffs > 0) return false;
    o.buffs = 1;
    s.cleanup_due = true;
    return true;
}

void discard(GameState& s, ObjId id, const Cause& cause) {
    const Object& o = s.obj(id);
    if (o.zone != Zone::Hand) throw std::logic_error("discard: not in hand");
    const PlayerId owner = o.owner;
    move_to(s, id, Zone::Trash, owner);
    emit(s, make_event(EventKind::Discarded, s.obj(id).ref(id), owner, cause.source));
}

void empower(GameState& s, ObjId id) {
    Object& o = s.obj(id);
    if (o.empowered) return;
    o.empowered = true;
    s.cleanup_due = true;
    emit(s, make_event(EventKind::Empowered, o.ref(id), controller_of(s, id)));
}

void disempower(GameState& s, ObjId id) {
    s.obj(id).empowered = false;
    s.cleanup_due = true;
}

void give(GameState& s, const Effect& e) {
    if (!s.alive(e.subject)) return;
    s.effects.push_back(e);
    s.cleanup_due = true;  // Might may have changed: lethal damage (323.4)
}

void give_might(GameState& s, ObjId subject, int delta, ObjRef source, Duration duration, std::optional<int> minimum) {
    if (minimum && delta < 0) delta = std::max(delta, std::min(0, *minimum - might(s, subject)));
    give(s, {.subject = s.obj(subject).ref(subject), .source = source, .kind = ModKind::Might,
             .value = static_cast<std::int16_t>(delta), .duration = duration});
}

void double_might(GameState& s, ObjId subject, ObjRef source, Duration duration) {
    give_might(s, subject, std::max(0, might(s, subject)), source, duration);
}

void expire(GameState& s, Duration d) {
    std::erase_if(s.effects, [&](const Effect& e) { return e.duration == d; });
    std::erase_if(s.delayed, [&](const Delayed& x) { return x.window == d; });
}

void prune(GameState& s) {
    const auto source_gone = [&](ObjRef r) { return !s.alive(r) || !on_board(s.obj(r.id).zone); };
    std::erase_if(s.effects, [&](const Effect& e) {
        return !s.alive(e.subject) || (e.duration == Duration::WhileSource && source_gone(e.source));
    });
    std::erase_if(s.delayed, [&](const Delayed& d) {
        return (d.subject.id != no_obj && !s.alive(d.subject)) ||
               (d.window == Duration::WhileSource && source_gone(d.source));
    });
    std::erase_if(s.links, [&](const Link& l) { return !s.alive(l.object); });
}

int uses(const GameState& s, ObjRef source, std::uint8_t ability) {
    for (const Use& u : s.uses) {
        if (u.source == source && u.ability == ability) return u.count;
    }
    return 0;
}

void use(GameState& s, ObjRef source, std::uint8_t ability, int delta) {
    for (Use& u : s.uses) {
        if (u.source == source && u.ability == ability) {
            u.count = static_cast<std::uint8_t>(std::clamp(u.count + delta, 0, 255));
            return;
        }
    }
    if (delta > 0) s.uses.push_back({source, ability, static_cast<std::uint8_t>(std::min(delta, 255))});
}

void link(GameState& s, ObjRef source, ObjRef object) { s.links.push_back({source, object}); }

std::vector<ObjId> linked(const GameState& s, ObjRef source) {
    std::vector<ObjId> out;
    for (const Link& l : s.links) {
        if (l.source == source && s.alive(l.object)) out.push_back(l.object.id);
    }
    return out;
}

} // namespace buddy::riftbound
