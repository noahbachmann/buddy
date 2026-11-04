#include "table.hpp"

#include <nlohmann/json.hpp>

namespace buddy::table {

using json = nlohmann::json;

std::vector<Event> Table::execute(const Command& c) {
    Event result;
    result.seq = c.seq;
    backend_.begin_command();
    try {
        switch (c.kind) {
            case CommandKind::Setup: result.cards = tracker_.setup(c.legend, c.champion, c.deck, c.runes); break;
            case CommandKind::Deal: result.cards = tracker_.deal(c.from, c.to, c.n); break;
            case CommandKind::Peek: result.cards = {tracker_.peek()}; break;
            case CommandKind::PeekDone: tracker_.peek_done(c.to); break;
            case CommandKind::Play: result.cards = {tracker_.play(c.id, c.to)}; break;
            case CommandKind::Move: tracker_.move(c.id, c.to); break;
            case CommandKind::Exhaust: tracker_.exhaust(c.ids, c.exhausted); break;
            case CommandKind::ShuffleTrashIntoDeck: tracker_.shuffle_trash_into_deck(c.order); break;
            case CommandKind::Token: result.cards = {tracker_.token(c.code, c.to)}; break;
            case CommandKind::Remove: tracker_.remove(c.id); break;
            case CommandKind::RescanHand: tracker_.rescan_hand(); break;
        }
    } catch (const Failure& f) {
        result = {.kind = EventKind::Failed, .seq = c.seq, .reason = f.reason, .message = f.what()};
    } catch (const std::exception& e) {  // the backend refused an op: piles and belief disagree
        result = {.kind = EventKind::Failed, .seq = c.seq, .reason = FailReason::Hardware, .message = e.what()};
    }
    std::vector<Event> out = tracker_.take_corrections();
    for (Event& e : out) e.seq = c.seq;
    out.push_back(std::move(result));
    return out;
}

json Table::state() const {
    const auto cards = [](const std::vector<Known>& list) {
        json a = json::array();
        for (const Known& k : list) {
            json c = {{"id", k.id}, {"code", k.code}};
            if (k.exhausted) c["exhausted"] = true;
            a.push_back(c);
        }
        return a;
    };
    const Belief& b = tracker_.belief();
    json spots = json::object();
    for (int p = 0; p < place_count; ++p) {
        const auto& list = b.spots[static_cast<std::size_t>(p)];
        if (!list.empty()) spots[std::string(to_string(static_cast<Place>(p)))] = cards(list);
    }
    const OpCounts& n = tracker_.counts();
    return {
        {"hand", cards(b.hand)}, {"deck", b.deck}, {"runes", b.runes}, {"spots", spots},
        {"held", b.held ? json{{"id", b.held->id}, {"code", b.held->code}} : json(nullptr)},
        {"ops", {{"scans", n.scans}, {"feeds", n.feeds}, {"takes", n.takes}, {"deals", n.deals},
                 {"passes", n.passes}, {"recoveries", n.recoveries}}},
    };
}

} // namespace buddy::table
