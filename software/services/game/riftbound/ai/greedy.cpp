#include "greedy.hpp"

#include <limits>

#include "riftbound/engine.hpp"
#include "riftbound/hidden.hpp"

namespace buddy::riftbound::ai {

namespace {

struct Search {
    PlayerId me;
    const GreedyOptions& options;
};

bool quiet(const GameState& s) {
    return s.winner || s.decision.kind == DecisionKind::None ||
           (s.decision.kind == DecisionKind::Priority && turn_state(s) == TurnState::NeutralOpen);
}

// Pass or decline if offered, else the first option (forced picks: damage assignment, trigger order).
Action declined(const std::vector<Action>& actions) {
    for (const Action& a : actions) {
        if (a.kind == ActionKind::Pass || a.kind == ActionKind::Done) return a;
    }
    return actions.front();
}

Action best(const GameState& s, const Search& c, int depth);

void play_on(GameState& s, const Search& c, int depth) {
    for (int n = 0; n < c.options.max_playout && !quiet(s); ++n) {
        const auto actions = legal_actions(s);
        const bool mine = s.decision.player == c.me && s.decision.kind != DecisionKind::Priority;
        // own Priority here = a closed state or a showdown: pass
        apply(s, actions.size() > 1 && mine && depth < c.options.depth ? best(s, c, depth + 1) : declined(actions));
    }
}

Action best(const GameState& s, const Search& c, int depth) {
    const auto actions = distinct(s, legal_actions(s));
    if (actions.size() == 1) return actions.front();
    Action choice = actions.front();
    double top = -std::numeric_limits<double>::infinity();
    bool first = true;
    for (const Action& a : actions) {
        double v = 0;
        if (a.kind == ActionKind::EndTurn) {
            v = eval(s, c.me, c.options.weights);
        } else {
            GameState t = s;
            apply(t, a);
            play_on(t, c, depth);
            v = eval(t, c.me, c.options.weights);
        }
        if (first || v > top) {
            choice = a;
            top = v;
            first = false;
        }
    }
    return choice;
}

} // namespace

Action greedy_choice(const GameState& s, const GreedyOptions& options) {
    if (s.decision.kind == DecisionKind::Mulligan) {
        return mulligan_choice(s, legal_actions(s), options.mulligan_energy);
    }
    return best(s, {s.decision.player, options}, 0);
}

Action mulligan_choice(const GameState& s, const std::vector<Action>& actions, int mulligan_energy) {
    for (const Action& a : actions) {
        if (a.kind == ActionKind::Pick && s.obj(a.obj).card->energy.value_or(0) >= mulligan_energy) return a;
    }
    return {ActionKind::Done};
}

void GreedyPlayer::begin(PlayerId, const Deck& seat0, const Deck& seat1) { decks_ = {seat0, seat1}; }

Action GreedyPlayer::choose(const GameState& view, const std::vector<Action>& actions) {
    if (view.decision.kind == DecisionKind::Mulligan) {
        return mulligan_choice(view, actions, options_.mulligan_energy);
    }
    return greedy_choice(determinize(view, decks_[0], decks_[1], rng_), options_);
}

} // namespace buddy::riftbound::ai
