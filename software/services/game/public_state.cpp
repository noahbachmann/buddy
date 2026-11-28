#include "public_state.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>

#include "riftbound/actions.hpp"
#include "riftbound/hidden.hpp"
#include "riftbound/tokens.hpp"
#include "table_moves.hpp"

namespace buddy::game {

using nlohmann::json;
using riftbound::Card;
using riftbound::CardType;
using riftbound::GameState;
using riftbound::no_obj;

namespace {

json domains(riftbound::Domains d) {
    json out = json::array();
    for (int i = 0; i < riftbound::domain_count; ++i) {
        const auto bit = static_cast<riftbound::Domains>(1u << i);
        if (d & bit) out.push_back(riftbound::domains_string(bit));
    }
    return out;
}

struct Builder {
    const GameState& pv;  // public_view: hidden cards are hidden_card()
    PlayerId buddy;

    std::string who(PlayerId p) const { return p == buddy ? "buddy" : "dev"; }
    bool hidden(ObjId id) const { return pv.obj(id).card == riftbound::hidden_card(); }

    json object(ObjId id) const {
        const auto& o = pv.obj(id);
        if (hidden(id)) return {{"id", id}, {"hidden", true}};
        json j = {{"id", id}, {"card", code_of(pv, id)}, {"exhausted", o.exhausted}};
        if (o.controller != o.owner) j["controller"] = who(o.controller);
        if (o.card->type == CardType::Unit) {
            j["might"] = riftbound::might(pv, id);
            j["damage"] = o.damage;
            if (o.buffs) j["buffs"] = o.buffs;
            if (o.stunned) j["stunned"] = true;
        }
        if (o.attached != no_obj) j["attached_to"] = o.attached;
        if (json fx = effects(id); !fx.empty()) j["effects"] = std::move(fx);
        if (o.empowered) j["empowered"] = true;
        if (riftbound::on_board(o.zone) || o.zone == riftbound::Zone::LegendZone) {
            json kw = json::array();
            const auto t = riftbound::traits(pv, id);
            for (int k = 0; k < riftbound::keyword_count; ++k) {
                const auto key = static_cast<riftbound::Keyword>(k);
                const int n = t.keywords.get(key);
                if (n == 0) continue;
                std::string name(riftbound::to_string(key));
                const bool valued = key == riftbound::Keyword::Assault || key == riftbound::Keyword::Shield ||
                                    key == riftbound::Keyword::Deflect;
                kw.push_back(valued && n > 1 ? name + " " + std::to_string(n) : name);
            }
            if (!kw.empty()) j["keywords"] = kw;
        }
        return j;
    }

    // Resolved spells' and abilities' effects on `id`, one entry per source in timestamp order: "Assault 2 · Ganking"
    json effects(ObjId id) const {
        json out = json::array();
        std::vector<std::pair<ObjId, std::vector<std::string>>> by_source;
        std::vector<riftbound::Duration> until;
        for (const auto& e : pv.effects) {
            if (e.subject.id != id || e.subject.gen != pv.obj(id).gen) continue;
            std::string what;
            const std::string kw(riftbound::to_string(e.keyword));
            switch (e.kind) {
                case riftbound::ModKind::Might:
                case riftbound::ModKind::RaiseMight:
                    what = (e.value >= 0 ? "+" : "−") + std::to_string(std::abs(e.value)) + " Might";
                    break;
                case riftbound::ModKind::BaseMight: what = "Might " + std::to_string(e.value); break;
                case riftbound::ModKind::Grant: what = e.value > 1 ? kw + " " + std::to_string(e.value) : kw; break;
                case riftbound::ModKind::Lose: what = "loses " + kw; break;
            }
            auto it = std::find_if(by_source.begin(), by_source.end(), [&](const auto& x) { return x.first == e.source.id; });
            if (it == by_source.end()) {
                by_source.push_back({e.source.id, {}});
                until.push_back(e.duration);
                it = by_source.end() - 1;
            }
            it->second.push_back(std::move(what));
        }
        for (std::size_t i = 0; i < by_source.size(); ++i) {
            const auto& [src, parts] = by_source[i];
            std::string text = parts.front();
            for (std::size_t k = 1; k < parts.size(); ++k) text += " · " + parts[k];
            json e = {{"text", text}};
            if (src < pv.objects.size() && !hidden(src)) e["source"] = code_of(pv, src);
            if (until[i] == riftbound::Duration::Turn) e["until"] = "turn";
            if (until[i] == riftbound::Duration::Combat) e["until"] = "combat";
            out.push_back(std::move(e));
        }
        return out;
    }

    json objects(const std::vector<ObjId>& ids) const {
        json out = json::array();
        for (const ObjId id : ids) out.push_back(object(id));
        return out;
    }

    json codes(const std::vector<ObjId>& ids) const {
        json out = json::array();
        for (const ObjId id : ids) out.push_back(hidden(id) ? json(nullptr) : json(code_of(pv, id)));
        return out;
    }

    json player(PlayerId p) const {
        const auto& pl = pv.player(p);
        std::vector<ObjId> base;
        std::map<std::string, std::pair<int, int>> runes;  // domain → ready, exhausted
        for (const ObjId id : pl.base) {
            const auto& o = pv.obj(id);
            if (!hidden(id) && o.card->type == CardType::Rune) {
                auto& [ready, exhausted] = runes[riftbound::domains_string(o.card->domains)];
                ++(o.exhausted ? exhausted : ready);
            } else {
                base.push_back(id);
            }
        }
        json rune_list = json::array();
        for (const auto& [domain, n] : runes) {
            rune_list.push_back({{"domain", domain}, {"ready", n.first}, {"exhausted", n.second}});
        }
        json power = json::object();
        for (int i = 0; i < riftbound::domain_count; ++i) {
            if (pl.pool.power[static_cast<std::size_t>(i)]) {
                power[riftbound::domains_string(static_cast<riftbound::Domains>(1u << i))] = pl.pool.power[static_cast<std::size_t>(i)];
            }
        }
        return {{"points", pl.points},
                {"legend", pl.legend.empty() ? json(nullptr) : object(pl.legend.front())},
                {"champion", objects(pl.champion)},
                {"base", objects(base)},
                {"runes", rune_list},
                {"pool", {{"energy", pl.pool.energy}, {"power", power}, {"any", pl.pool.universal}}},
                {"hand", pl.hand.size()},
                {"deck", pl.main_deck.size()},
                {"rune_deck", pl.rune_deck.size()},
                {"trash", codes(pl.trash)},
                {"banishment", codes(pl.banishment)}};
    }

    json battlefield(std::size_t b) const {
        const auto& f = pv.battlefields[b];
        json units = {{"buddy", json::array()}, {"dev", json::array()}};
        for (const ObjId id : f.here) units[who(pv.obj(id).controller)].push_back(object(id));
        return {{"id", f.card},
                {"card", code_of(pv, f.card)},
                {"owner", who(pv.obj(f.card).owner)},
                {"controller", f.controller ? json(who(*f.controller)) : json(nullptr)},
                {"contested", f.contested ? json(who(*f.contested)) : json(nullptr)},
                {"units", units},
                {"facedown", f.facedown.size()}};
    }

    json target(const riftbound::Target& t) const {
        if (t.ref.id == no_obj) return {{"player", who(t.player)}};
        return {{"id", t.ref.id}, {"card", hidden(t.ref.id) ? json(nullptr) : json(code_of(pv, t.ref.id))}};
    }

    json chain() const {
        json out = json::array();
        for (const auto& item : pv.chain) {
            static constexpr const char* kinds[] = {"card", "activated", "triggered"};
            const ObjId card = item.card != no_obj ? item.card : item.source.id;
            json targets = json::array();
            for (const auto& t : item.targets) targets.push_back(target(t));
            out.push_back({{"kind", kinds[static_cast<int>(item.kind)]},
                           {"card", hidden(card) ? json(nullptr) : json(code_of(pv, card))},
                           {"controller", who(item.controller)},
                           {"finalized", item.finalized},
                           {"targets", targets}});
        }
        return out;
    }
};

// The cards Dev can still name for a prompt, with how many copies are left.
json candidates(const Session& session, const GameState& s, const DevPrompt& p) {
    const auto& deck = session.config().decks[riftbound::opponent(session.buddy())];
    json out = json::array();
    if (p.kind == DevPrompt::Kind::Champion) {  // #98: the decklist's champion first
        for (const Card* c : riftbound::champion_options(deck)) out.push_back({{"code", c->home().code.str()}, {"left", 1}});
        return out;
    }
    if (p.kind == DevPrompt::Kind::Battlefield) {
        for (const Card* c : deck.battlefields) out.push_back({{"code", c->home().code.str()}, {"left", 1}});
        return out;
    }
    const bool runes = p.kind == DevPrompt::Kind::Runes;
    const auto keep = session.kept();
    const auto pool = dev_pool(s, session.buddy(), keep, deck, runes);
    for (const Card* c : pool) {
        int left = 0;
        for (ObjId id = 0; id < s.objects.size(); ++id) {
            const auto& o = s.obj(id);
            if (o.card != c || o.owner == session.buddy() || o.known & 1u << session.buddy()) continue;
            if (o.zone != riftbound::Zone::Hand && o.zone != riftbound::Zone::MainDeck && o.zone != riftbound::Zone::RuneDeck) continue;
            if (std::find(keep.begin(), keep.end(), id) == keep.end()) ++left;
        }
        json e = {{"code", c->home().code.str()}, {"left", left}};
        if (runes) e["domain"] = riftbound::domains_string(c->domains);
        out.push_back(e);
    }
    return out;
}

json card_json(const Card& c) {
    json j = {{"name", c.name}, {"type", riftbound::to_string(c.type)}, {"domains", domains(c.domains)},
              {"tags", c.tags}, {"text", c.text}};
    if (c.energy) j["cost"] = *c.energy;
    if (c.power) j["power"] = *c.power;
    if (c.might) j["might"] = *c.might;
    if (c.champion) j["champion"] = true;
    if (c.token) j["token"] = true;
    return j;
}

} // namespace

json public_state(const Session& session) { return public_state(session, session.state(), session.dev_options()); }

json public_state(const Session& session, const GameState& s, const std::vector<DevOption>& options) {
    const GameState pv = riftbound::public_view(s);
    const Builder b{pv, session.buddy()};
    const PlayerId dev = riftbound::opponent(session.buddy());
    const auto& d = pv.decision;

    json j = {{"status", to_string(session.status())},
              {"winner", pv.winner ? json(b.who(*pv.winner)) : json(nullptr)},
              {"turn", pv.turn},
              {"phase", riftbound::to_string(pv.phase)},
              {"turn_player", b.who(pv.turn_player)},
              {"victory", riftbound::victory_score},
              {"players", {{"buddy", b.player(session.buddy())}, {"dev", b.player(dev)}}},
              {"chain", b.chain()}};
    if (session.status() == Status::Paused) {
        j["pause"] = {{"reason", to_string(session.pause())}, {"message", pause_text(session.pause())},
                      {"resumable", session.resumable()}};
    }
    if (d.kind != riftbound::DecisionKind::None) {
        j["decision"] = {{"player", b.who(d.player)}, {"kind", riftbound::to_string(d.kind)},
                         {"purpose", riftbound::to_string(d.purpose)}};
    }
    json fields = json::array();
    for (std::size_t i = 0; i < pv.battlefields.size(); ++i) fields.push_back(b.battlefield(i));
    j["battlefields"] = fields;
    if (pv.showdown) {
        const auto& sd = *pv.showdown;
        j["showdown"] = {{"battlefield", sd.where}, {"combat", sd.combat}, {"focus", b.who(sd.focus)},
                         {"attacker", sd.combat ? json(b.who(sd.attacker)) : json(nullptr)}};
    }
    if (session.status() == Status::DevToAct) {
        json list = json::array();
        for (std::size_t i = 0; i < options.size(); ++i) {
            const auto& o = options[i];
            json e = {{"i", i}, {"text", option_text(pv, o)}, {"group", option_group(o)}};
            if (o.card) e["card"] = o.card->home().code.str();
            // the public object it acts on: the panel puts the option on that card (never a hidden one: placeholder)
            const ObjId obj = o.action.obj;
            if (obj != no_obj && obj < pv.objects.size() && !b.hidden(obj)) e["obj"] = obj;
            list.push_back(e);
        }
        j["options"] = list;
    }
    if (const DevPrompt* p = session.prompt()) {
        j["prompt"] = {{"kind", to_string(p->kind)}, {"n", p->objs.size()}, {"candidates", candidates(session, s, *p)}};
        if (!p->known.empty()) {
            const GameState pv = riftbound::public_view(s);
            json known = json::array();
            for (const ObjId id : p->known) {
                if (pv.obj(id).card != riftbound::hidden_card()) known.push_back(code_of(pv, id));
            }
            j["prompt"]["known"] = known;
        }
    }
    json log = json::array();
    for (const auto& l : session.log()) {
        json e = {{"turn", l.turn}, {"player", b.who(l.player)}, {"text", l.text}};
        if (!l.cards.empty()) e["cards"] = l.cards;
        log.push_back(std::move(e));
    }
    j["log"] = log;
    return j;
}

json catalogue(const riftbound::Deck& seat0, const riftbound::Deck& seat1) {
    json out = json::object();
    const auto add = [&](const Card* c) {
        if (c) out[c->token ? "token:" + c->name : c->home().code.str()] = card_json(*c);
    };
    for (const auto* deck : {&seat0, &seat1}) {
        add(deck->legend);
        add(deck->champion);
        for (const auto& e : deck->main) add(e.card);
        for (const auto& e : deck->runes) add(e.card);
        for (const Card* c : deck->battlefields) add(c);
    }
    for (const Card* t : {&riftbound::tokens::bird(), &riftbound::tokens::shadow_clone(), &riftbound::tokens::gold()}) add(t);
    return out;
}

} // namespace buddy::game
