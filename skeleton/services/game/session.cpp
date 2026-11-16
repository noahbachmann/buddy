#include "session.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>

#include "riftbound/engine.hpp"
#include "riftbound/hidden.hpp"

namespace buddy::game {

using riftbound::GameState;
using riftbound::no_obj;
using table::CommandKind;
using table::EventKind;
using table::FailReason;
using table::Place;

std::string_view to_string(Status s) {
    switch (s) {
        case Status::Setup: return "setup";
        case Status::DevToAct: return "dev_to_act";
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
    }
    return "?";
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
    }
    return Pause::Hardware;
}

std::string pause_line(Pause p) {
    switch (p) {
        case Pause::Unreadable: return "I can't read one of my cards. Please check the scanner, then resume.";
        case Pause::Missing: return "One of my cards is missing. Please put it back on my hand pile, then resume.";
        case Pause::Desync: return "Something went wrong with my cards. The game can't go on, I saved the replay.";
        default: return "My table needs a hand. Please check it, then resume.";
    }
}

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
        s_ = riftbound::new_game(cfg_.decks[0], cfg_.decks[1], {.seed = cfg_.seed});
        log = rec.take();
    }
    setup_ = table_setup(s_, log, cfg_.buddy);
    opening_.emplace();
    opening_->moves = moves_;
    opening_->list = split(opening_->moves.step(s_, log));
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

void Session::on_dev(const Action& a) {
    if (status_ != Status::DevToAct) throw std::logic_error("session: not Dev's decision");
    const auto actions = riftbound::legal_actions(s_);
    if (std::find(actions.begin(), actions.end(), a) == actions.end()) {
        throw riftbound::IllegalAction("session: " + riftbound::format_action(a) + " isn't legal now");
    }
    begin_step(a, false);
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
    if (status_ != Status::Paused || pause_ == Pause::Desync) throw std::logic_error("session: nothing to resume");
    ++stats_.resumes;
    pause_ = Pause::None;
    pause_message_.clear();
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
                send_step_command();
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
    GameState next = s_;
    std::vector<TableMove> list;
    TableMoves moves = moves_;
    try {
        std::vector<riftbound::LoggedMove> log;
        {
            MoveRecorder rec;
            riftbound::apply(next, *st.action);
            log = rec.take();
        }
        list = split(moves.step(next, log));
    } catch (const std::exception& e) {
        do_pause(Pause::Desync, std::string("re-applying ") + riftbound::format_action(*st.action) + ": " + e.what());
        return false;
    }
    if (on_moves) on_moves(list);
    for (std::size_t i = 0; i < st.done; ++i) {
        if (i < list.size() && same(list[i], st.list[i])) continue;
        do_pause(Pause::Desync, "command " + std::to_string(i + 1) + " was " + describe_move(st.list[i]) + ", now " +
                                    (i < list.size() ? describe_move(list[i]) : "nothing"));
        return false;
    }
    st.next = std::move(next);
    st.moves = std::move(moves);
    st.list = std::move(list);
    return true;
}

void Session::send(table::Command c, Kind kind, int attempt) {
    c.seq = ++seq_;
    ++stats_.commands;
    inflight_ = Inflight{c, kind, attempt};
    out_.table.push_back(std::move(c));
}

void Session::send_step_command() {
    const TableMove& m = step_->list[step_->done];
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
    const TableMove m = step_->list[step_->done];
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
    ++stats_.binds;
    bound_.add(s_, obj);
    riftbound::Reveal r;
    r.before = replay_.actions.size();
    r.obj = obj;
    r.code = code;
    replay_.reveals.push_back(r);
    emit_replay(riftbound::format_reveal(r));
    if (changed && step_ && step_->action) step_->stale = true;
}

void Session::commit() {
    Step st = std::move(*step_);
    step_.reset();
    if (st.action) {
        s_ = std::move(st.next);
        replay_.actions.push_back(*st.action);
        emit_replay(riftbound::format_action(*st.action));
        re_decides_ = 0;
    }
    moves_ = std::move(st.moves);
    ++stats_.steps;
    reconcile_hand();
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
    if (status_ == Status::Paused && pause_ == Pause::Desync) return;  // the first reason stays
    ++stats_.pauses;
    status_ = Status::Paused;
    pause_ = p;
    pause_message_ = message;
    if (p == Pause::Desync) emit_replay("# desync: " + message);
    out_.voice.push_back(pause_line(p));
}

} // namespace buddy::game
