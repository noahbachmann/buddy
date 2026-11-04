#include "table_moves.hpp"

#include <algorithm>
#include <memory>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "riftbound/ai/greedy.hpp"
#include "riftbound/ai/player.hpp"
#include "riftbound/engine.hpp"
#include "riftbound/hidden.hpp"

namespace buddy::game {

using json = nlohmann::json;
using riftbound::CardType;
using riftbound::Pos;
using riftbound::Zone;
using table::CommandKind;
using table::Place;

namespace {

bool board(Zone z) { return riftbound::on_board(z); }

// Where a card stops between two places within one step: the chain (a unit being played), set aside (mulligan).
bool transit(const GameState& s, const LoggedMove& m) {
    return m.to == Zone::Chain || (m.to == Zone::Removed && !s.obj(m.id).card->token);
}

Place place_of(Zone z, std::uint8_t where, Pos pos) {
    switch (z) {
        case Zone::Hand: return Place::Hand;
        case Zone::MainDeck: return pos == Pos::Bottom ? Place::DeckBottom : Place::DeckTop;
        case Zone::RuneDeck:
            if (pos != Pos::Bottom) break;
            return Place::RunesBottom;
        case Zone::Base: return Place::Base;
        case Zone::Battlefield: return where == 0 ? Place::Bf0 : Place::Bf1;
        case Zone::Facedown: return where == 0 ? Place::Fd0 : Place::Fd1;
        case Zone::Chain: return Place::Chain;
        case Zone::Trash: return Place::Trash;
        case Zone::Banishment: return Place::Banish;
        case Zone::ChampionZone: return Place::Champion;
        case Zone::LegendZone: return Place::Legend;
        case Zone::Removed: break;
    }
    throw std::logic_error("table: no place for a card ending a step in the " + std::string(riftbound::to_string(z)));
}

TableMove command(CommandKind kind, Place to, std::vector<ObjId> objs) {
    TableMove m;
    m.cmd.kind = kind;
    m.cmd.to = to;
    m.objs = std::move(objs);
    return m;
}

} // namespace

riftbound::Action nearest_copy(const GameState& s, riftbound::Action a) {
    if (a.kind != riftbound::ActionKind::Play || a.obj == riftbound::no_obj) return a;
    const auto& o = s.obj(a.obj);
    if (o.zone != Zone::Hand) return a;
    for (const ObjId id : s.player(o.owner).hand) {
        if (s.obj(id).card != o.card) continue;
        a.obj = id;  // hand order = pile order bottom → top: the first copy is the nearest
        break;
    }
    return a;
}

std::string code_of(const GameState& s, ObjId id) {
    const riftbound::Card& c = *s.obj(id).card;
    if (c.token) return "token:" + c.name;
    return c.home().code.str();
}

TableSetup table_setup(const GameState& s, std::vector<LoggedMove>& log, PlayerId seat) {
    TableSetup t;
    std::size_t n = 0;
    for (; n < log.size() && log[n].created; ++n) {
        const auto& o = s.obj(log[n].id);
        if (o.owner != seat || o.card->type == CardType::Battlefield) continue;
        switch (log[n].to) {
            case Zone::LegendZone: t.legend = log[n].id; break;
            case Zone::ChampionZone: t.champion = log[n].id; break;
            case Zone::MainDeck: t.deck.push_back(log[n].id); break;
            case Zone::RuneDeck: t.runes.push_back(log[n].id); break;
            default: throw std::logic_error("table_setup: card created in the " + std::string(to_string(log[n].to)));
        }
    }
    log.erase(log.begin(), log.begin() + static_cast<std::ptrdiff_t>(n));
    return t;
}

std::vector<TableMove> TableMoves::step(const GameState& after, const std::vector<LoggedMove>& log) {
    // the seat's cards, hops through transit places collapsed into one move at the time of the last hop
    std::vector<LoggedMove> moves;
    for (const LoggedMove& e : log) {
        const auto& o = after.obj(e.id);
        if (o.owner != seat_ || o.card->type == CardType::Battlefield) continue;
        const auto last = std::find_if(moves.rbegin(), moves.rend(), [&](const LoggedMove& m) { return m.id == e.id; });
        if (last == moves.rend() || !transit(after, *last)) {
            moves.push_back(e);
            continue;
        }
        LoggedMove merged = *last;
        moves.erase(std::next(last).base());
        merged.to = e.to;
        merged.to_where = e.to_where;
        merged.pos = e.pos;
        const bool nowhere = merged.from == merged.to && merged.from_where == merged.to_where;
        const bool fleeting = merged.created && merged.to == Zone::Removed;  // a token made and gone in one step
        if (!(nowhere && !merged.created) && !fleeting) moves.push_back(merged);
    }

    std::vector<TableMove> out;
    exhausted_.resize(after.objects.size(), 0);
    for (const LoggedMove& e : moves) {
        const ObjId id = e.id;
        if (e.created) {
            TableMove m = command(CommandKind::Token, place_of(e.to, e.to_where, e.pos), {id});
            m.cmd.code = code_of(after, id);
            out.push_back(std::move(m));
        } else if (e.from == Zone::MainDeck && e.to == Zone::MainDeck) {  // Vision: looked at, then to the bottom
            out.push_back(command(CommandKind::Peek, Place::DeckTop, {id}));
            out.push_back(command(CommandKind::PeekDone, place_of(e.to, e.to_where, e.pos), {}));
        } else if (e.from == Zone::MainDeck || e.from == Zone::RuneDeck) {
            const auto from = e.from == Zone::MainDeck ? table::Source::Deck : table::Source::Runes;
            const Place to = place_of(e.to, e.to_where, e.pos);
            if (!out.empty() && out.back().cmd.kind == CommandKind::Deal && out.back().cmd.from == from &&
                out.back().cmd.to == to) {
                ++out.back().cmd.n;
                out.back().objs.push_back(id);
            } else {
                TableMove m = command(CommandKind::Deal, to, {id});
                m.cmd.from = from;
                out.push_back(std::move(m));
            }
        } else if (e.to == Zone::Removed) {
            if (!after.obj(id).card->token) throw std::logic_error("table: a card set aside at the end of a step");
            out.push_back(command(CommandKind::Remove, Place::Base, {id}));
        } else {
            const auto kind = e.from == Zone::Hand ? CommandKind::Play : CommandKind::Move;
            out.push_back(command(kind, place_of(e.to, e.to_where, e.pos), {id}));
        }
        if (!(board(e.from) && board(e.to)) || e.created) exhausted_[id] = 0;  // arrives ready, like table's keeps_exhausted
    }

    std::vector<ObjId> exhaust, ready;
    for (ObjId id = 0; id < after.objects.size(); ++id) {
        const auto& o = after.obj(id);
        if (o.owner != seat_ || o.card->type == CardType::Battlefield) continue;
        if (!board(o.zone) && o.zone != Zone::LegendZone && o.zone != Zone::ChampionZone) continue;
        if (o.exhausted == (exhausted_[id] != 0)) continue;
        (o.exhausted ? exhaust : ready).push_back(id);
        exhausted_[id] = o.exhausted ? 1 : 0;
    }
    for (auto* ids : {&exhaust, &ready}) {
        if (ids->empty()) continue;
        TableMove m = command(CommandKind::Exhaust, Place::Base, *ids);
        m.cmd.exhausted = ids == &exhaust;
        out.push_back(std::move(m));
    }
    return out;
}

namespace {

json line_of(const TableMove& m) { return {{"cmd", table::to_json(m.cmd)}, {"objs", m.objs}}; }

} // namespace

WorkloadGame workload_game(const riftbound::Deck& seat0, const riftbound::Deck& seat1, PlayerId seat,
                           std::uint64_t seed, bool greedy) {
    namespace ai = riftbound::ai;
    WorkloadGame w;
    try {
        GameState s;
        std::vector<LoggedMove> log;
        {
            MoveRecorder rec;
            s = riftbound::new_game(seat0, seat1, {.seed = seed});
            log = rec.take();
        }
        const TableSetup t = table_setup(s, log, seat);
        json codes = json::array(), rune_codes = json::array();
        for (const ObjId id : t.deck) codes.push_back(code_of(s, id));
        for (const ObjId id : t.runes) rune_codes.push_back(code_of(s, id));
        w.lines.push_back({{"setup",
                            {{"seat", seat}, {"seed", seed}, {"legend", code_of(s, t.legend)},
                             {"champion", code_of(s, t.champion)}, {"deck", codes}, {"runes", rune_codes},
                             {"objs", {{"legend", t.legend}, {"champion", t.champion}, {"deck", t.deck},
                                       {"runes", t.runes}}}}}});
        TableMoves moves(seat);
        for (const TableMove& m : moves.step(s, log)) w.lines.push_back(line_of(m));

        std::array<std::unique_ptr<ai::Player>, riftbound::player_count> players;
        for (PlayerId p = 0; p < riftbound::player_count; ++p) {
            const std::uint64_t ps = seed * 2 + p;
            if (greedy) players[p] = std::make_unique<ai::GreedyPlayer>(ps);
            else players[p] = std::make_unique<ai::RandomPlayer>(ps);
            players[p]->begin(p, seat0, seat1);
        }
        for (int n = 0; !s.winner; ++n) {
            if (n == 100000) throw std::logic_error("no winner");
            const auto actions = riftbound::legal_actions(s);
            const PlayerId p = s.decision.player;
            riftbound::Action a =
                actions.size() == 1 ? actions[0] : players[p]->choose(riftbound::view(s, p), actions);
            if (p == seat) a = nearest_copy(s, a);
            {
                MoveRecorder rec;
                riftbound::apply(s, a);
                log = rec.take();
            }
            for (const TableMove& m : moves.step(s, log)) w.lines.push_back(line_of(m));
        }
        w.lines.push_back({{"end", {{"winner", *s.winner}, {"turns", s.turn}}}});
    } catch (const std::exception& e) {
        w.problem = e.what();
    }
    return w;
}

} // namespace buddy::game
