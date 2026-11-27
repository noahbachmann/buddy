#include "session.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <tuple>

#include "riftbound/engine.hpp"
#include "riftbound/hidden.hpp"
#include "riftbound/rules.hpp"

namespace buddy::game {

using riftbound::ActionKind;
using riftbound::Card;
using riftbound::CardType;
using riftbound::Decision;
using riftbound::DecisionKind;
using riftbound::GameState;
using riftbound::no_obj;
using riftbound::Zone;
using table::CommandKind;
using table::EventKind;
using table::FailReason;
using table::Place;

std::string_view to_string(Status s) {
    switch (s) {
        case Status::Setup: return "setup";
        case Status::DevToAct: return "dev_to_act";
        case Status::DevToReveal: return "dev_to_reveal";
        case Status::BuddyThinking: return "thinking";
        case Status::Acting: return "acting";
        case Status::Paused: return "paused";
        case Status::Over: return "over";
    }
    return "?";
}

std::string_view to_string(Pause p) {
    switch (p) {
        case Pause::None: return "none";
        case Pause::Unreadable: return "unreadable";
        case Pause::Hardware: return "hardware";
        case Pause::Missing: return "missing";
        case Pause::Invalid: return "invalid";
        case Pause::Desync: return "desync";
        case Pause::Timeout: return "timeout";
        case Pause::TableRestart: return "table_restart";
    }
    return "?";
}

std::string_view to_string(DevPrompt::Kind k) {
    switch (k) {
        case DevPrompt::Kind::Battlefield: return "battlefield";
        case DevPrompt::Kind::Hand: return "hand";
        case DevPrompt::Kind::Deck: return "deck";
        case DevPrompt::Kind::Runes: return "runes";
    }
    return "?";
}

std::string describe(const GameState& s, const DevOption& o) {
    if (!o.card) return riftbound::describe(s, o.action);
    if (o.discards()) return riftbound::describe(s, o.action) + " (discard " + o.card->name + ")";
    switch (o.action.kind) {
        case ActionKind::Play: return "play " + o.card->name;
        case ActionKind::Pick: return "choose " + o.card->name;
        default: return riftbound::describe(s, o.action) + " (" + o.card->name + ")";
    }
}

std::string option_text(const GameState& pv, const DevOption& o) {
    const Action& a = o.action;
    if (o.card && !o.discards()) return describe(pv, o);
    const bool hidden = a.obj < pv.objects.size() && pv.obj(a.obj).card == riftbound::hidden_card();
    if (hidden) return pv.decision.kind == DecisionKind::Mulligan ? "set aside a card" : "a card";  // by count
    const std::string discard = o.discards() ? " (discard " + o.card->name + ")" : "";
    if (a.kind == ActionKind::Activate) {
        const std::string& name = pv.obj(a.obj).card->name;
        const auto* spec = riftbound::ability_spec(pv, a.obj, a.n);
        if (spec && spec->builtin == riftbound::Builtin::Empower) return name + ": Empower" + discard;
        if (spec && spec->builtin == riftbound::Builtin::Equip) return name + ": Equip" + discard;
        if (spec && spec->add) return name + ": Add" + discard;
        return name + ": activate" + discard;
    }
    return riftbound::describe(pv, a) + discard;
}

std::string_view option_group(const DevOption& o) {
    switch (o.action.kind) {
        case ActionKind::Play: return "play";
        case ActionKind::Activate: return "activate";
        case ActionKind::Move: return "move";
        case ActionKind::Pass:
        case ActionKind::EndTurn: return "pass";
        case ActionKind::Pick:
        case ActionKind::Done: return "choose";
    }
    return "choose";
}

bool dev_hidden(const GameState& s, PlayerId buddy, const std::set<ObjId>& named, ObjId obj) {
    if (obj == no_obj || obj >= s.objects.size()) return false;
    const auto& o = s.obj(obj);
    return o.owner != buddy && o.zone == Zone::Hand && !(o.known & 1u << buddy) && !named.contains(obj);
}

std::vector<const Card*> dev_pool(const GameState& s, PlayerId buddy, std::span<const ObjId> keep,
                                  const riftbound::Deck& deck, bool runes) {
    const PlayerId dev = riftbound::opponent(buddy);
    std::set<const Card*> held;
    for (ObjId id = 0; id < s.objects.size(); ++id) {
        const auto& o = s.obj(id);
        if (o.owner != dev || o.known & 1u << buddy || (o.card->type == CardType::Rune) != runes) continue;
        if (o.zone != (runes ? Zone::RuneDeck : Zone::MainDeck) && o.zone != Zone::Hand) continue;
        if (std::find(keep.begin(), keep.end(), id) != keep.end()) continue;
        held.insert(o.card);
    }
    std::vector<const Card*> out;
    for (const auto& e : runes ? deck.runes : deck.main) {
        if (held.contains(e.card) && std::find(out.begin(), out.end(), e.card) == out.end()) out.push_back(e.card);
    }
    return out;
}

namespace {

// An action whose cost discards a spell (Mel's Empower): Dev declares the spell with the option (#73).
bool needs_spell(const GameState& s, const Action& a) {
    if (a.kind != ActionKind::Activate) return false;
    const auto* spec = riftbound::ability_spec(s, a.obj, a.n);
    return spec && spec->cost.discard_spell;
}

} // namespace

// Actions on cards Buddy knows as they are; Dev's hidden hand cards stand for any card Dev could hold (the decklist
// minus what Buddy has seen): one option per such card, checked on a copy where the first hidden hand card (`h`)
// holds it. Mulligan: by count (which cards go back stays secret). Priority: an action whose cost discards a spell
// comes once per spell Dev could pay with: the spells Buddy knows in Dev's hand, and the pool's spells on `h` (#73;
// whether the placeholders hold one decides nothing). The order depends on nothing the placeholders hold (#50: `i`
// is stable and leaks nothing): by group, declared cards in decklist order, the rest by object and index.
std::vector<DevOption> dev_options(const GameState& s, const DevContext& c) {
    const auto base = riftbound::legal_actions(s);
    const auto& d = s.decision;
    const bool priority = d.kind == DecisionKind::Priority;
    const auto hidden = [&](ObjId id) { return dev_hidden(s, c.buddy, *c.named, id); };
    std::vector<DevOption> out;
    ObjId h = no_obj;
    for (const Action& a : base) {
        if (hidden(a.obj)) {
            if (h == no_obj) {
                h = a.obj;  // the first hidden one offered: picks go in option order
                if (d.kind == DecisionKind::Mulligan) out.push_back({a});
            }
        } else if (priority && needs_spell(s, a)) {
            // with a spell Buddy knows in hand: its legality holds whatever the placeholders are
            std::set<const Card*> seen;
            for (const ObjId id : s.player(d.player).hand) {
                const Card* card = s.obj(id).card;
                if (!hidden(id) && card->type == CardType::Spell && seen.insert(card).second) out.push_back({a, id, card});
            }
        } else {
            out.push_back({a});
        }
    }
    if (priority) {
        h = no_obj;
        for (const ObjId id : s.player(d.player).hand) {
            if (hidden(id)) {
                h = id;
                break;
            }
        }
    }
    if (h != no_obj && d.kind != DecisionKind::Mulligan) {
        bool spells_only = false;  // a "discard a spell" cost: its options were filtered on the placeholders
        if (d.kind == DecisionKind::Choose && d.purpose == riftbound::Purpose::Discard) {
            for (const auto& item : s.chain) {
                if (item.finalized) continue;
                spells_only = item.cost.discard_spell;
                break;
            }
        }
        for (const Card* card : dev_pool(s, c.buddy, c.keep, *c.dev_deck, false)) {
            if (spells_only && card->type != CardType::Spell) continue;
            GameState copy = s;
            if (!riftbound::bind(copy, h, card, c.buddy, c.keep)) continue;
            if (!priority) {  // a Choose: its options are fixed, h is one of them
                out.push_back({{ActionKind::Pick, h}, h, card});
                continue;
            }
            for (const Action& a : riftbound::legal_actions(copy)) {
                if (a.obj == h && a.kind == ActionKind::Play) {
                    out.push_back({a, h, card});
                } else if (card->type == CardType::Spell && !hidden(a.obj) && needs_spell(copy, a) &&
                           std::none_of(out.begin(), out.end(), [&](const DevOption& o) { return o.action == a && o.card == card; })) {
                    out.push_back({a, h, card});  // a known spell of the same card came first
                }
            }
        }
    }
    // Stable order: the rank of the group, then declared cards by decklist, then the action itself (a known card's id
    // and index don't depend on the placeholders).
    const auto& main = c.dev_deck->main;
    const auto rank = [&](const DevOption& o) {
        static constexpr int by_kind[] = {/*Pick*/ 3, /*Done*/ 5, /*Play*/ 0, /*EndTurn*/ 4, /*Pass*/ 4,
                                          /*Activate*/ 1, /*Move*/ 2};
        int listed = -1;
        if (o.card) {
            listed = static_cast<int>(std::find_if(main.begin(), main.end(), [&](const auto& e) { return e.card == o.card; }) - main.begin());
        }
        return std::tuple(by_kind[static_cast<int>(o.action.kind)], listed, o.action.obj, o.action.n);
    };
    std::stable_sort(out.begin(), out.end(), [&](const DevOption& a, const DevOption& b) { return rank(a) < rank(b); });
    return out;
}

namespace {

bool into_a_deck(Place p) { return p == Place::DeckTop || p == Place::DeckBottom || p == Place::RunesBottom; }

// One deal per card: a deal that fails halfway reports none of the cards it placed.
std::vector<TableMove> split(std::vector<TableMove> list) {
    std::vector<TableMove> out;
    for (TableMove& m : list) {
        if (m.cmd.kind != CommandKind::Deal || m.objs.size() <= 1) {
            out.push_back(std::move(m));
            continue;
        }
        for (const ObjId id : m.objs) {
            TableMove one = m;
            one.cmd.n = 1;
            one.objs = {id};
            out.push_back(std::move(one));
        }
    }
    return out;
}

bool same(const TableMove& a, const TableMove& b) { return a.cmd == b.cmd && a.objs == b.objs; }

std::string describe_move(const TableMove& m) {
    std::string out(table::to_string(m.cmd.kind));
    for (const ObjId id : m.objs) out += " " + std::to_string(id);
    if (m.cmd.kind != CommandKind::Exhaust && m.cmd.kind != CommandKind::Peek) {
        out += " → " + std::string(table::to_string(m.cmd.to));
    }
    return out;
}

Pause pause_of(FailReason r) {
    switch (r) {
        case FailReason::NotInHand: return Pause::Missing;
        case FailReason::Unreadable: return Pause::Unreadable;
        case FailReason::Hardware: return Pause::Hardware;
        case FailReason::Invalid: return Pause::Invalid;
        case FailReason::NoGame: return Pause::TableRestart;
    }
    return Pause::Hardware;
}

}  // namespace

std::string pause_text(Pause p) {
    switch (p) {
        case Pause::Unreadable: return "I can't read one of my cards. Please check the scanner, then resume.";
        case Pause::Missing: return "One of my cards is missing. Please put it back on my hand pile, then resume.";
        case Pause::Desync: return "Something went wrong with my cards. The game can't go on, I saved the replay.";
        case Pause::Timeout: return "My table isn't answering. Please check it, then resume.";
        case Pause::TableRestart: return "My table restarted and lost my cards. The game can't go on, please concede.";
        default: return "My table needs a hand. Please check it, then resume.";
    }
}

namespace {

// "Vi", "Vi and the Bird", "Vi, Zed and two Birds": tokens get "the", copies are counted.
std::string spoken_list(const GameState& pv, const std::vector<ObjId>& ids) {
    static const char* const count[] = {"", "", "two", "three", "four", "five", "six"};
    std::vector<std::pair<const Card*, int>> cards;
    for (const ObjId id : ids) {
        const Card* c = pv.obj(id).card;
        const auto it = std::find_if(cards.begin(), cards.end(), [&](const auto& e) { return e.first == c; });
        if (it == cards.end()) cards.emplace_back(c, 1);
        else ++it->second;
    }
    std::string out;
    for (std::size_t i = 0; i < cards.size(); ++i) {
        const auto& [c, n] = cards[i];
        if (i > 0) out += i + 1 == cards.size() ? " and " : ", ";
        if (c == riftbound::hidden_card()) out += n == 1 ? "a card" : std::string(count[std::min(n, 6)]) + " cards";
        else if (n > 1) out += std::string(count[std::min(n, 6)]) + " " + c->name + "s";
        else out += (c->token ? "the " : "") + c->name;
    }
    return out;
}

std::string battlefield_name(const GameState& s, std::uint8_t b) { return s.obj(s.battlefields.at(b).card).card->name; }

} // namespace

Session::Session(const riftbound::CardDb& db, SessionConfig cfg) : db_(db), cfg_(std::move(cfg)), moves_(cfg_.buddy) {
    replay_.seed = cfg_.seed;
    replay_.buddy = cfg_.buddy;
    replay_.decks = cfg_.deck_texts;
}

int Session::table_id(ObjId obj) const {
    const auto it = id_.find(obj);
    return it == id_.end() ? 0 : it->second;
}

ObjId Session::obj_of(int id) const {
    const auto it = obj_.find(id);
    return it == obj_.end() ? no_obj : it->second;
}

std::string Session::replay_text() const {
    std::string out;
    for (const auto& l : lines_) out += l + "\n";
    return out;
}

void Session::emit_replay(const std::string& line) {
    lines_.push_back(line);
    out_.replay.push_back(line);
}

void Session::map_id(ObjId obj, int id) {
    forget(obj);
    if (const auto it = obj_.find(id); it != obj_.end()) id_.erase(it->second);
    id_[obj] = id;
    obj_[id] = obj;
}

void Session::forget(ObjId obj) {
    const auto it = id_.find(obj);
    if (it == id_.end()) return;
    obj_.erase(it->second);
    id_.erase(it);
}

// ---- inputs ----

void Session::start() {
    if (status_ != Status::Setup || inflight_) throw std::logic_error("session: already started");
    std::vector<riftbound::LoggedMove> log;
    {
        MoveRecorder rec;
        const auto dev = static_cast<std::uint8_t>(1u << riftbound::opponent(cfg_.buddy));
        s_ = riftbound::new_game(cfg_.decks[0], cfg_.decks[1], {.seed = cfg_.seed, .ask_discard = dev});
        log = rec.take();
    }
    setup_ = table_setup(s_, log, cfg_.buddy);
    opening_.emplace();
    opening_->moves = moves_;
    for (const auto& bf : s_.battlefields) {  // Dev's battlefield first: before both mulligans (113)
        if (s_.obj(bf.card).owner != cfg_.buddy) {
            opening_->list.push_back({{}, DevPrompt{DevPrompt::Kind::Battlefield, {bf.card}}});
        }
    }
    for (TableMove& m : split(opening_->moves.step(s_, log))) opening_->list.push_back({std::move(m), std::nullopt});
    opening_->next = s_;  // unused: the opening's binds go on s_ directly

    std::istringstream header(riftbound::format_header(replay_));
    for (std::string line; std::getline(header, line);) emit_replay(line);

    table::Command c;
    c.kind = CommandKind::Setup;
    c.legend = code_of(s_, setup_.legend);
    c.champion = code_of(s_, setup_.champion);
    c.deck = static_cast<int>(setup_.deck.size());
    c.runes = static_cast<int>(setup_.runes.size());
    for (const ObjId id : setup_.deck) c.sim_deck.push_back(code_of(s_, id));
    for (const ObjId id : setup_.runes) c.sim_runes.push_back(code_of(s_, id));
    out_.voice.push_back(s_.first_player == cfg_.buddy ? "Let's play Riftbound. I go first."
                                                       : "Let's play Riftbound. You go first.");
    setup_cmd_ = c;
    send(std::move(c), Kind::Setup);
}

void Session::on_table(const table::Event& e) {
    if (!inflight_ || e.seq != inflight_->cmd.seq) return;
    if (status_ == Status::Paused && pause_ == Pause::Timeout) {  // the late answer: go on as if it had come in time
        status_ = waiting_;
        pause_ = Pause::None;
        pause_message_.clear();
    }
    if (e.kind == EventKind::HandCorrected) {
        correction(e);
        return;
    }
    const Inflight f = *inflight_;
    inflight_.reset();
    if (e.kind == EventKind::Done) done(f, e);
    else failed(f, e);
    if (pause_after_ && status_ != Status::Paused && status_ != Status::Over) {
        pause_after_ = false;
        do_pause(Pause::Missing, "cards missing from the hand");
    }
    pump();
}

void Session::on_dev(const DevOption& o) {
    if (status_ != Status::DevToAct) throw std::logic_error("session: not Dev's decision");
    if (std::find(dev_options_.begin(), dev_options_.end(), o) == dev_options_.end()) {
        ++stats_.refusals;
        throw Refused("not one of your options: " + riftbound::format_action(o.action));
    }
    ++stats_.dev_decisions;
    take_dev(o);
    pump();
}

const DevPrompt* Session::prompt() const {
    if (status_ != Status::DevToReveal || !step_ || step_->done >= step_->list.size()) return nullptr;
    const auto& p = step_->list[step_->done].prompt;
    return p ? &*p : nullptr;
}

void Session::on_reveal(const std::vector<const Card*>& cards) {
    if (prompt() == nullptr) throw std::logic_error("session: nothing to name");
    const DevPrompt prompt_copy = *prompt();  // the step's list is replaced below
    const DevPrompt* p = &prompt_copy;
    const auto refuse = [&](const std::string& why) {
        ++stats_.refusals;
        throw Refused(why);
    };
    if (cards.size() != p->objs.size()) {
        refuse("expected " + std::to_string(p->objs.size()) + " card(s), got " + std::to_string(cards.size()));
    }
    GameState s = s_;
    riftbound::BindMemory bound = bound_;
    bool changed = false;
    for (std::size_t i = 0; i < cards.size(); ++i) {
        const Card* c = cards[i];
        if (c == nullptr) refuse("no card given");
        const bool rune = c->type == CardType::Rune, field = c->type == CardType::Battlefield;
        if (rune != (p->kind == DevPrompt::Kind::Runes) || field != (p->kind == DevPrompt::Kind::Battlefield)) {
            refuse(c->name + " can't come from your " + std::string(to_string(p->kind)));
        }
        const ObjId obj = p->objs[i];
        changed = changed || s.obj(obj).card != c;
        if (!riftbound::bind(s, obj, c, cfg_.buddy, bound.keep(s, cfg_.buddy))) {
            refuse(field ? c->name + " isn't one of your battlefields"
                         : "you have no " + c->name + " left that I haven't seen");
        }
        bound.add(s, obj);
    }
    Step& st = *step_;
    if (st.action && changed) {
        Step trial;
        trial.action = st.action;
        if (const auto why = compute(s, st.done + 1, trial); !why.empty()) {
            refuse("that can't be: " + why);
        }
        st.next = std::move(trial.next);
        st.moves = std::move(trial.moves);
        st.list = std::move(trial.list);
        ++stats_.reapplies;
    }
    s_ = std::move(s);
    bound_ = std::move(bound);
    for (std::size_t i = 0; i < cards.size(); ++i) bind_line(p->objs[i], cards[i]);
    ++stats_.prompts;
    ++st.done;
    status_ = Status::Acting;
    pump();
}

void Session::on_ai(std::uint64_t id, const Action& a) {
    if (status_ != Status::BuddyThinking || id != ai_id_) return;
    if (std::find(ai_actions_.begin(), ai_actions_.end(), a) == ai_actions_.end()) {
        throw std::logic_error("session: the AI answered " + riftbound::format_action(a) + ", not offered");
    }
    begin_step(nearest_copy(s_, a), true);
    pump();
}

void Session::resume() {
    if (!resumable()) throw std::logic_error("session: nothing to resume");
    ++stats_.resumes;
    const bool timeout = pause_ == Pause::Timeout;
    pause_ = Pause::None;
    pause_message_.clear();
    if (timeout) {  // the same command, the same seq: the table answers it once, run or not
        status_ = waiting_;
        out_.table.push_back(inflight_->cmd);
        return;
    }
    status_ = Status::Acting;
    attempt_ = 0;
    if (!set_up_) {  // nothing laid out yet: the setup again
        send(setup_cmd_, Kind::Setup);
        return;
    }
    table::Command c;
    c.kind = CommandKind::RescanHand;
    send(std::move(c), Kind::Rescan);  // then the step goes on where it failed
}

void Session::table_timeout() {
    if (!inflight_ || status_ == Status::Paused || status_ == Status::Over) return;
    waiting_ = status_;
    do_pause(Pause::Timeout, "no answer to " + std::string(table::to_string(inflight_->cmd.kind)) + " seq " +
                                 std::to_string(inflight_->cmd.seq));
}

void Session::table_restarted() {
    if (status_ == Status::Over || (status_ == Status::Paused && !resumable())) return;
    inflight_.reset();  // its answer is gone with the table
    do_pause(Pause::TableRestart, "the table restarted");
}

std::optional<AiRequest> Session::ai_request() const {
    if (status_ != Status::BuddyThinking) return std::nullopt;
    return AiRequest{ai_id_, riftbound::view(s_, cfg_.buddy), ai_actions_};
}

void Session::concede(PlayerId p) {
    if (status_ == Status::Over) return;
    riftbound::concede(s_, p);
    step_.reset();
    inflight_.reset();
    status_ = Status::Over;
    emit_replay("# player " + std::to_string(p) + " conceded");
    out_.voice.push_back(p == cfg_.buddy ? "I concede. Well played." : "You concede. Good game.");
}

// ---- flow ----

void Session::pump() {
    while (!inflight_ && status_ != Status::Paused && status_ != Status::Over) {
        if (!fixes_.empty()) {
            table::Command c;
            c.kind = CommandKind::Play;
            c.id = fixes_.front();
            c.to = Place::DeckTop;
            send(std::move(c), Kind::Fix);
            return;
        }
        if (step_) {
            if (step_->stale) {
                step_->stale = false;
                ++stats_.reapplies;
                if (!recompute()) return;
            }
            if (step_->done < step_->list.size()) {
                if (step_->list[step_->done].prompt) status_ = Status::DevToReveal;
                else send_step_command();
                return;
            }
            commit();
            continue;
        }
        if (!advance()) return;
    }
}

bool Session::advance() {
    if (s_.winner) {
        status_ = Status::Over;
        out_.voice.push_back(*s_.winner == cfg_.buddy ? "I win. Good game." : "You win. Well played.");
        return false;
    }
    if (s_.decision.player != cfg_.buddy) {
        // the card Dev declared for a discard cost pays it (#73); the next Priority ends the declaration
        if (declared_ != no_obj && s_.decision.kind == DecisionKind::Priority) declared_ = no_obj;
        if (declared_ != no_obj && s_.decision.kind == DecisionKind::Choose &&
            s_.decision.purpose == riftbound::Purpose::Discard) {
            const Action pick{ActionKind::Pick, declared_};
            declared_ = no_obj;
            const auto legal = riftbound::legal_actions(s_);
            if (std::find(legal.begin(), legal.end(), pick) != legal.end()) {
                ++stats_.dev_forced;
                take_dev({pick});
                return true;
            }
        }
        dev_options_ = options();
        if (dev_options_.size() == 1) {  // no card in Dev's hand could change it
            ++stats_.dev_forced;
            take_dev(dev_options_[0]);
            return true;
        }
        status_ = Status::DevToAct;
        return false;
    }
    auto actions = riftbound::legal_actions(s_);
    if (actions.size() == 1) {  // forced: not worth a question
        begin_step(actions[0], false);
        return true;
    }
    status_ = Status::BuddyThinking;
    ai_actions_ = actions;
    out_.ai = AiRequest{++ai_id_, riftbound::view(s_, cfg_.buddy), std::move(actions)};
    return false;
}

void Session::begin_step(const Action& a, bool chosen) {
    step_.emplace();
    step_->action = a;
    step_->chosen = chosen;
    status_ = Status::Acting;
    recompute();
}

bool Session::recompute() {
    Step& st = *step_;
    Step fresh;
    fresh.action = st.action;
    if (const auto why = compute(s_, st.done, fresh); !why.empty()) {
        do_pause(Pause::Desync, why);
        return false;
    }
    st.next = std::move(fresh.next);
    st.moves = std::move(fresh.moves);
    st.list = std::move(fresh.list);
    return true;
}

std::string Session::compute(const GameState& base, std::size_t through, Step& out) const {
    const Step& st = *step_;
    out.next = base;
    out.moves = moves_;
    try {
        std::vector<riftbound::LoggedMove> log;
        {
            MoveRecorder rec;
            riftbound::apply(out.next, *out.action);
            log = rec.take();
        }
        auto list = split(out.moves.step(out.next, log));
        if (on_moves) on_moves(list);
        out.list = items(base, out.next, log, std::move(list));
    } catch (const std::exception& e) {
        return std::string("re-applying ") + riftbound::format_action(*out.action) + ": " + e.what();
    }
    const auto describe_item = [](const Item& it) {
        if (!it.prompt) return describe_move(it.move);
        std::string d = "prompt " + std::string(to_string(it.prompt->kind));
        for (const ObjId id : it.prompt->objs) d += " " + std::to_string(id);
        return d;
    };
    for (std::size_t i = 0; i < through && i < st.list.size(); ++i) {
        const Item* now = i < out.list.size() ? &out.list[i] : nullptr;
        const Item& was = st.list[i];
        if (now && now->prompt.has_value() == was.prompt.has_value() &&
            (was.prompt ? *now->prompt == *was.prompt : same(now->move, was.move))) {
            continue;
        }
        return "command " + std::to_string(i + 1) + " was " + describe_item(was) + ", now " +
               (now ? describe_item(*now) : "nothing");
    }
    return {};
}

// Buddy's commands and Dev's prompts in log order. A prompt: an object of Dev's that Buddy didn't know before the
// step and knows after it, unless Dev named it; its place in the log is where it first lands in public (else its
// first move). Prompts in a row from the same place are one prompt (two burned cards, the channelled runes).
std::vector<Session::Item> Session::items(const GameState& base, const GameState& next,
                                          const std::vector<riftbound::LoggedMove>& log,
                                          std::vector<TableMove> moves) const {
    const auto bit = static_cast<std::uint8_t>(1u << cfg_.buddy);
    std::vector<std::pair<std::size_t, ObjId>> found;  // log index, obj
    std::set<ObjId> seen;
    for (std::size_t k = 0; k < log.size(); ++k) {
        const ObjId id = log[k].id;
        if (id >= base.objects.size() || !seen.insert(id).second) continue;  // created this step: a token
        const auto& was = base.obj(id);
        if (was.owner == cfg_.buddy || was.known & bit || !(next.obj(id).known & bit) || named_.contains(id)) continue;
        std::size_t at = k;
        for (std::size_t j = k; j < log.size(); ++j) {
            if (log[j].id == id && riftbound::is_public(log[j].to)) {
                at = j;
                break;
            }
        }
        found.emplace_back(at, id);
    }
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<Item> out;
    std::size_t f = 0;
    const auto add_prompt = [&](ObjId id) {
        const Zone z = base.obj(id).zone;
        const auto kind = z == Zone::Hand       ? DevPrompt::Kind::Hand
                          : z == Zone::RuneDeck ? DevPrompt::Kind::Runes
                                                : DevPrompt::Kind::Deck;
        if (!out.empty() && out.back().prompt && out.back().prompt->kind == kind) {
            out.back().prompt->objs.push_back(id);
        } else {
            out.push_back({{}, DevPrompt{kind, {id}}});
        }
    };
    for (TableMove& m : moves) {
        for (; f < found.size() && found[f].first < m.at; ++f) add_prompt(found[f].second);
        out.push_back({std::move(m), std::nullopt});
    }
    for (; f < found.size(); ++f) add_prompt(found[f].second);
    return out;
}

void Session::send(table::Command c, Kind kind, int attempt) {
    c.seq = ++seq_;
    ++stats_.commands;
    inflight_ = Inflight{c, kind, attempt};
    out_.table.push_back(std::move(c));
}

void Session::send_step_command() {
    const TableMove& m = step_->list[step_->done].move;
    table::Command c = m.cmd;
    const auto id = [&](ObjId obj) {
        const int t = table_id(obj);
        if (t == 0) do_pause(Pause::Desync, "the table has no id for object " + std::to_string(obj));
        return t;
    };
    switch (c.kind) {
        case CommandKind::Play:
        case CommandKind::Move:
        case CommandKind::Remove:
            c.id = id(m.objs.at(0));
            break;
        case CommandKind::Exhaust:
            for (const ObjId o : m.objs) c.ids.push_back(id(o));
            break;
        default:
            break;
    }
    if (status_ == Status::Paused) return;
    send(std::move(c), Kind::Step, attempt_);
}

void Session::done(const Inflight& f, const table::Event& e) {
    switch (f.kind) {
        case Kind::Setup:
            if (e.cards.size() < 2) {
                do_pause(Pause::Invalid, "setup gave no legend and champion ids");
                return;
            }
            map_id(setup_.legend, e.cards[0].id);
            map_id(setup_.champion, e.cards[1].id);
            set_up_ = true;
            step_ = std::move(opening_);
            opening_.reset();
            status_ = Status::Acting;
            return;
        case Kind::Fix:
            fixes_.pop_front();
            std::erase(table_hand_, f.cmd.id);
            ++stats_.extras;
            return;
        case Kind::Rescan:
            if (!missing_.empty()) {
                do_pause(Pause::Missing, "cards still missing from the hand");
                return;
            }
            if (!step_) reconcile_hand();  // else at the commit: the hand has the step's cards
            return;
        case Kind::Step:
            break;
    }

    attempt_ = 0;
    const TableMove m = step_->list[step_->done].move;
    const auto& c = m.cmd;
    switch (c.kind) {
        case CommandKind::Deal:
        case CommandKind::Peek:
        case CommandKind::Token:
            for (std::size_t i = 0; i < m.objs.size() && i < e.cards.size(); ++i) {
                map_id(m.objs[i], e.cards[i].id);
                if (c.kind == CommandKind::Deal && c.to == Place::Hand) table_hand_.push_back(e.cards[i].id);
            }
            if (c.kind == CommandKind::Peek) peeking_ = m.objs.at(0);
            break;
        case CommandKind::Play:
            std::erase(table_hand_, f.cmd.id);
            if (into_a_deck(c.to)) forget(m.objs[0]);
            break;
        case CommandKind::Move:
            if (c.to == Place::Hand) table_hand_.push_back(f.cmd.id);
            if (into_a_deck(c.to)) forget(m.objs[0]);
            break;
        case CommandKind::Remove:
            forget(m.objs[0]);
            break;
        case CommandKind::PeekDone:
            forget(peeking_);
            peeking_ = no_obj;
            break;
        default:
            break;
    }
    ++step_->done;
    if (c.kind == CommandKind::Deal || c.kind == CommandKind::Peek) {
        for (std::size_t i = 0; i < m.objs.size() && i < e.cards.size(); ++i) reveal(m.objs[i], e.cards[i].code);
    }
}

void Session::failed(const Inflight& f, const table::Event& e) {
    const std::string what = std::string(table::to_string(f.cmd.kind)) + " failed " +
                             std::string(table::to_string(e.reason)) + ": " + e.message;
    if (f.kind == Kind::Step && e.reason == FailReason::NotInHand && f.cmd.kind == CommandKind::Play &&
        step_->done == 0 && step_->chosen && re_decides_ < 2) {
        // the card Buddy chose isn't there (a misread at draw, now corrected): nothing happened yet, decide again
        ++re_decides_;
        ++stats_.re_decides;
        step_.reset();
        reconcile_hand();
        if (status_ != Status::Paused) status_ = Status::BuddyThinking;
        return;
    }
    if (f.kind == Kind::Step && e.reason == FailReason::Unreadable && f.attempt == 0) {
        ++stats_.retries;
        attempt_ = 1;  // pump sends the step's command again
        return;
    }
    do_pause(pause_of(e.reason), what);  // a failed step command or fix stays next: resume runs it again
}

void Session::correction(const table::Event& e) {
    ++stats_.corrections;
    table_hand_.clear();
    for (const auto& c : e.hand) table_hand_.push_back(c.id);
    bool misread = false;
    for (const auto& r : e.replaced) {
        const ObjId obj = obj_of(r.id);
        if (obj == no_obj) {
            do_pause(Pause::Desync, "a misread card the engine doesn't know: table id " + std::to_string(r.id));
            return;
        }
        ++stats_.misreads;
        misread = true;
        reveal(obj, r.code);
    }
    if (misread) out_.voice.push_back("I misread a card. Fixed.");
    for (const int id : e.missing) {
        const ObjId obj = obj_of(id);
        if (obj == no_obj) continue;
        missing_[obj] = code_of(s_, obj);
        forget(obj);
    }
    for (const auto& x : e.extra) {
        // a lost card found again: the same object; anything else isn't the engine's: back on the deck top
        const auto lost = std::find_if(missing_.begin(), missing_.end(), [&](const auto& m) { return m.second == x.code; });
        if (lost != missing_.end()) {
            map_id(lost->first, x.id);
            missing_.erase(lost);
        } else {
            fixes_.push_back(x.id);
        }
    }
    if (!missing_.empty()) pause_after_ = true;
}

void Session::reveal(ObjId obj, const std::string& code) {
    const auto found = db_.find_code(code);
    if (found.card == nullptr) {
        do_pause(Pause::Desync, "the table read " + code + ": " + found.error);
        return;
    }
    const bool changed = s_.obj(obj).card != found.card;
    if (!riftbound::bind(s_, obj, found.card, cfg_.buddy, bound_.keep(s_, cfg_.buddy))) {
        do_pause(Pause::Desync, "no unknown " + found.card->name + " left for object " + std::to_string(obj));
        return;
    }
    bound_.add(s_, obj);
    bind_line(obj, found.card);
    if (changed && step_ && step_->action) step_->stale = true;
}

void Session::commit() {
    Step st = std::move(*step_);
    step_.reset();
    if (st.action) {
        const GameState before = std::exchange(s_, std::move(st.next));
        std::erase_if(named_, [&](ObjId id) { return s_.obj(id).known & 1u << cfg_.buddy; });
        replay_.actions.push_back(*st.action);
        emit_replay(riftbound::format_action(*st.action));
        re_decides_ = 0;
        narrate(before, *st.action);
    }
    moves_ = std::move(st.moves);
    ++stats_.steps;
    reconcile_hand();
}

// The log line of every committed action, Buddy's voice line per action group and the scoring lines (#50).
void Session::narrate(const GameState& before, const Action& a) {
    const PlayerId who = before.decision.player;
    GameState pv = riftbound::public_view(s_);
    {
        const Decision now = std::exchange(pv.decision, before.decision);  // describe() reads the decision answered
        std::string text = riftbound::describe(pv, a);
        for (std::size_t at; (at = text.find("hidden card")) != std::string::npos;) text.replace(at, 11, "a card");
        log_.push_back({before.turn, who, std::move(text)});
        if (log_.size() > log_lines) log_.pop_front();
        pv.decision = now;
    }

    if (who == cfg_.buddy) {
        const auto kind = before.decision.kind;
        if (kind == DecisionKind::Priority &&
            (a.kind == ActionKind::Play || a.kind == ActionKind::Activate || a.kind == ActionKind::Move)) {
            flush_voice();
            VoiceGroup g;
            g.action = a;
            if (a.kind == ActionKind::Move) {
                const auto& at = a.n == riftbound::to_base ? before.player(cfg_.buddy).base : before.battlefields[a.n].here;
                for (const ObjId id : at) {
                    if (before.obj(id).controller == cfg_.buddy) g.before.push_back(id);
                    else if (before.obj(id).card->type == CardType::Unit) g.attack = true;
                }
            }
            group_ = std::move(g);
        }
        if (group_ && group_->action.kind != ActionKind::Move) {  // its targets so far, forced ones included
            for (const auto& item : s_.chain) {
                const bool mine = group_->action.kind == ActionKind::Play
                                      ? item.card == group_->action.obj
                                      : item.kind == riftbound::ItemKind::Activated && item.source.id == group_->action.obj;
                if (mine) group_->targets = item.targets;
            }
        }
    }
    const auto& d = s_.decision;
    if (group_ && (s_.winner || d.player != cfg_.buddy || (d.kind != DecisionKind::Choose && d.kind != DecisionKind::Recycle))) {
        flush_voice();
    }

    // Scoring: a battlefield bit new this turn. Holds happen in the turn player's Beginning Phase, conquers later.
    const bool new_turn = s_.turn != before.turn;
    for (PlayerId p = 0; p < riftbound::player_count; ++p) {
        const auto was = new_turn ? 0 : before.player(p).scored;
        const auto now = s_.player(p).scored & ~was;
        for (std::uint8_t b = 0; b < s_.battlefields.size(); ++b) {
            if (!(now & 1u << b)) continue;
            const bool hold = p == s_.turn_player && (new_turn || before.phase < riftbound::Phase::Main);
            const std::string verb = hold ? "hold " : "conquer ";
            out_.voice.push_back((p == cfg_.buddy ? "I " : "You ") + verb + battlefield_name(s_, b) + ". " +
                                 std::to_string(s_.player(p).points) + " to " +
                                 std::to_string(s_.player(riftbound::opponent(p)).points) + ".");
        }
    }
}

// "I play Zed to Zaun Warrens", "I play Void Rush on Vi", "I move Vi and the Bird to Zaun Warrens", "I attack at
// Zaun Warrens with Vi", "I empower Mel": names on the public view (a card Buddy plays is public by then).
void Session::flush_voice() {
    if (!group_) return;
    const VoiceGroup g = std::move(*group_);
    group_.reset();
    const GameState pv = riftbound::public_view(s_);
    const auto name = [&](ObjId id) { return spoken_list(pv, {id}); };
    std::string line;
    switch (g.action.kind) {
        case ActionKind::Play: {
            line = "I play " + name(g.action.obj);
            const auto& o = s_.obj(g.action.obj);
            if (o.zone == Zone::Battlefield) line += " to " + battlefield_name(s_, o.where);
            break;
        }
        case ActionKind::Activate: {
            const auto* spec = riftbound::ability_spec(s_, g.action.obj, g.action.n);
            const auto builtin = spec ? spec->builtin : riftbound::Builtin::None;
            if (builtin == riftbound::Builtin::Empower) return out_.voice.push_back("I empower " + name(g.action.obj));
            line = (builtin == riftbound::Builtin::Equip ? "I equip " : "I activate ") + name(g.action.obj);
            break;
        }
        case ActionKind::Move: {
            const bool home = g.action.n == riftbound::to_base;
            const auto& at = home ? s_.player(cfg_.buddy).base : s_.battlefields[g.action.n].here;
            std::vector<ObjId> moved;
            for (const ObjId id : at) {
                if (s_.obj(id).controller == cfg_.buddy && s_.obj(id).card->type == CardType::Unit &&
                    std::find(g.before.begin(), g.before.end(), id) == g.before.end()) {
                    moved.push_back(id);
                }
            }
            if (moved.empty()) return;
            const std::string where = home ? "my base" : battlefield_name(s_, g.action.n);
            out_.voice.push_back(g.attack ? "I attack at " + where + " with " + spoken_list(pv, moved)
                                          : "I move " + spoken_list(pv, moved) + " to " + where);
            return;
        }
        default: return;
    }
    std::vector<ObjId> objs;
    std::vector<std::string> players;
    for (const auto& t : g.targets) {
        if (t.ref.id == no_obj) players.push_back(t.player == cfg_.buddy ? "myself" : "you");
        else if (std::find(objs.begin(), objs.end(), t.ref.id) == objs.end()) objs.push_back(t.ref.id);
    }
    std::string on = objs.empty() ? "" : spoken_list(pv, objs);
    for (const auto& pl : players) on += (on.empty() ? "" : " and ") + pl;
    if (!on.empty()) line += (g.action.kind == ActionKind::Activate && line.starts_with("I equip") ? " to " : " on ") + on;
    out_.voice.push_back(line);
}

void Session::bind_line(ObjId obj, const Card* card) {
    ++stats_.binds;
    riftbound::Reveal r;
    r.kind = riftbound::Reveal::Kind::Bind;
    r.before = replay_.actions.size();
    r.obj = obj;
    r.code = card->home().code.str();
    replay_.reveals.push_back(r);
    emit_replay(riftbound::format_reveal(r));
}

std::vector<DevOption> Session::options() const {
    const auto keep = bound_.keep(s_, cfg_.buddy);
    return game::dev_options(s_, {cfg_.buddy, keep, &named_, &cfg_.decks[riftbound::opponent(cfg_.buddy)]});
}

void Session::take_dev(const DevOption& o) {
    if (o.card && dev_hidden(s_, cfg_.buddy, named_, o.obj)) {
        if (!riftbound::bind(s_, o.obj, o.card, cfg_.buddy, bound_.keep(s_, cfg_.buddy))) {
            throw std::logic_error("session: Dev's option " + o.card->name + " no longer binds");  // options() checked it
        }
        bound_.add(s_, o.obj);
        named_.insert(o.obj);
        ++stats_.dev_named;
        bind_line(o.obj, o.card);
    }
    if (o.discards()) {
        declared_ = o.obj;
        ++stats_.dev_declared;
    }
    dev_options_.clear();
    begin_step(o.action, false);
}

// The engine's hand order = the table's (a disturbed pile, found by a recovery).
void Session::reconcile_hand() {
    if (!missing_.empty()) return;
    std::vector<ObjId> want;
    for (const int id : table_hand_) {
        const ObjId obj = obj_of(id);
        if (obj == no_obj) {
            do_pause(Pause::Desync, "the table's hand holds card " + std::to_string(id) + ", unknown to the engine");
            return;
        }
        want.push_back(obj);
    }
    if (want == s_.player(cfg_.buddy).hand) return;
    if (!riftbound::set_hand_order(s_, cfg_.buddy, want)) {
        do_pause(Pause::Desync, "the table's hand isn't the engine's");
        return;
    }
    ++stats_.reorders;
    riftbound::Reveal r;
    r.kind = riftbound::Reveal::Kind::Order;
    r.before = replay_.actions.size();
    r.player = cfg_.buddy;
    r.ids = want;
    replay_.reveals.push_back(r);
    emit_replay(riftbound::format_reveal(r));
}

void Session::do_pause(Pause p, const std::string& message) {
    if (status_ == Status::Paused && !resumable()) return;  // a reason for good stays
    ++stats_.pauses;
    status_ = Status::Paused;
    pause_ = p;
    pause_message_ = message;
    if (p == Pause::Desync) emit_replay("# desync: " + message);
    out_.voice.push_back(pause_text(p));
}

} // namespace buddy::game
