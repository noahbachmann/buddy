#include "ismcts.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

#include "greedy.hpp"
#include "riftbound/engine.hpp"
#include "riftbound/hidden.hpp"

namespace buddy::riftbound::ai {

namespace {

constexpr std::uint32_t no_node = 0;  // the root is never a child

struct Edge {
    ActionKey key;
    std::uint32_t child = no_node;
    PlayerId who = 0;  // who chose it: rewards are from their side
    int visits = 0;
    int available = 0;
    double reward = 0;
};

struct Node {
    std::vector<Edge> edges;
};

// The root's moves: the given actions, merged by key on the view. The same ids are legal in every determinization
// (#27), so the root applies these instead of keys found in a determinization (a hidden card's key would differ).
struct Root {
    std::vector<Action> actions;
    std::vector<ActionKey> keys;
    std::vector<std::size_t> index;  // into the caller's list
};

using Clock = std::chrono::steady_clock;

class Tree {
public:
    Tree(const GameState& view, const Root& root, const Deck& seat0, const Deck& seat1, std::uint64_t seed,
         const IsmctsOptions& o)
        : view_(view), root_(root), seat0_(seat0), seat1_(seat1), rng_(seed), o_(o), me_(view.decision.player) {
        nodes_.emplace_back();
    }

    void iterate() {
        GameState s = determinize(view_, seat0_, seat1_, rng_);
        path_.clear();
        std::uint32_t node = 0;
        while (!s.winner) {
            std::vector<Action> actions;
            std::vector<ActionKey> keys;
            if (node == 0) {
                actions = root_.actions;
                keys = root_.keys;
            } else {
                actions = legal_actions(s);
                if (actions.size() == 1) {  // forced: no node, as in the arena
                    apply(s, actions.front());
                    continue;
                }
                actions = distinct(s, actions);
                keys.reserve(actions.size());
                for (const Action& a : actions) keys.push_back(key_of(s, a));
            }
            // Untried here → expand one of them and stop descending.
            untried_.clear();
            for (std::size_t i = 0; i < keys.size(); ++i) {
                if (find(node, keys[i]) < 0) untried_.push_back(i);
            }
            if (!untried_.empty()) {
                const std::size_t i = untried_[rng_.below(untried_.size())];
                const auto child = static_cast<std::uint32_t>(nodes_.size());
                nodes_.emplace_back();
                nodes_[node].edges.push_back({keys[i], child, s.decision.player, 0, 0, 0});
                for (std::size_t k = 0; k < keys.size(); ++k) {
                    if (k != i && find(node, keys[k]) >= 0) ++nodes_[node].edges[find(node, keys[k])].available;
                }
                ++nodes_[node].edges.back().available;
                path_.push_back({node, nodes_[node].edges.size() - 1});
                apply(s, actions[i]);
                break;
            }
            // All tried: UCB1 with availability counts over what this determinization offers.
            std::size_t pick = 0;
            int pick_edge = -1;
            double top = -1;
            for (std::size_t k = 0; k < keys.size(); ++k) {
                const int e = find(node, keys[k]);
                Edge& edge = nodes_[node].edges[static_cast<std::size_t>(e)];
                ++edge.available;
                const double v = edge.reward / edge.visits +
                                 o_.exploration * std::sqrt(std::log(edge.available) / edge.visits);
                if (v > top) {
                    top = v;
                    pick = k;
                    pick_edge = e;
                }
            }
            path_.push_back({node, static_cast<std::size_t>(pick_edge)});
            node = nodes_[node].edges[static_cast<std::size_t>(pick_edge)].child;
            apply(s, actions[pick]);
        }
        playout(s);
        const double r = reward(s);
        for (const auto& [n, e] : path_) {
            Edge& edge = nodes_[n].edges[e];
            ++edge.visits;
            edge.reward += edge.who == me_ ? r : 1 - r;
        }
    }

    // Visits per root action.
    std::vector<int> root_visits() const {
        std::vector<int> v(root_.keys.size());
        for (std::size_t i = 0; i < v.size(); ++i) {
            const int e = find(0, root_.keys[i]);
            v[i] = e < 0 ? 0 : nodes_[0].edges[static_cast<std::size_t>(e)].visits;
        }
        return v;
    }

private:
    int find(std::uint32_t node, const ActionKey& key) const {
        const auto& edges = nodes_[node].edges;
        for (std::size_t i = 0; i < edges.size(); ++i) {
            if (edges[i].key == key) return static_cast<int>(i);
        }
        return -1;
    }

    // Random actions within the turn, then pass/decline until someone has Neutral Open priority: eval doesn't read
    // a half-resolved chain or showdown.
    void playout(GameState& s) {
        const int turn = s.turn;
        for (int n = 0; n < o_.playout && !s.winner && s.turn == turn; ++n) {
            const auto actions = legal_actions(s);
            apply(s, actions[rng_.below(actions.size())]);
        }
        for (int n = 0; n < 400 && !s.winner && s.decision.kind != DecisionKind::None; ++n) {
            if (s.decision.kind == DecisionKind::Priority && turn_state(s) == TurnState::NeutralOpen) break;
            const auto actions = legal_actions(s);
            const auto it = std::find_if(actions.begin(), actions.end(), [](const Action& a) {
                return a.kind == ActionKind::Pass || a.kind == ActionKind::Done;
            });
            apply(s, it != actions.end() ? *it : actions.front());
        }
    }

    double reward(const GameState& s) const {
        if (s.winner) return *s.winner == me_ ? 1 : 0;
        return 1 / (1 + std::exp(-eval(s, me_, o_.weights) / o_.scale));
    }

    const GameState& view_;
    const Root& root_;
    const Deck& seat0_;
    const Deck& seat1_;
    Rng rng_;
    const IsmctsOptions& o_;
    PlayerId me_;
    std::vector<Node> nodes_;
    std::vector<std::pair<std::uint32_t, std::size_t>> path_;
    std::vector<std::size_t> untried_;
};

} // namespace

IsmctsResult ismcts_search(const GameState& view, const std::vector<Action>& actions, const Deck& seat0,
                           const Deck& seat1, std::uint64_t seed, const IsmctsOptions& options) {
    Root root;
    for (std::size_t i = 0; i < actions.size(); ++i) {
        const ActionKey k = key_of(view, actions[i]);
        if (std::find(root.keys.begin(), root.keys.end(), k) != root.keys.end()) continue;
        root.actions.push_back(actions[i]);
        root.keys.push_back(k);
        root.index.push_back(i);
    }
    IsmctsResult result{actions.front(), 0, std::vector<int>(actions.size())};
    if (root.actions.size() == 1) return result;

    const int threads = std::max(1, options.threads);
    const bool timed = options.budget_ms > 0;
    const auto deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                             std::chrono::duration<double, std::milli>(options.budget_ms));
    std::vector<std::vector<int>> visits(static_cast<std::size_t>(threads));
    std::vector<int> done(static_cast<std::size_t>(threads));
    auto work = [&](int t) {
        // per thread: own tree and Rng, a fixed share of the iterations (reproducible whatever the scheduling)
        Rng mix(seed + 0x9e3779b97f4a7c15ULL * static_cast<std::uint64_t>(t));
        Tree tree(view, root, seat0, seat1, mix.next(), options);
        const int share = options.iterations / threads + (t < options.iterations % threads);
        int n = 0;
        while (timed ? (n == 0 || Clock::now() < deadline) : n < share) {
            tree.iterate();
            ++n;
        }
        visits[static_cast<std::size_t>(t)] = tree.root_visits();
        done[static_cast<std::size_t>(t)] = n;
    };
    {
        std::vector<std::jthread> pool;
        for (int t = 1; t < threads; ++t) pool.emplace_back(work, t);
        work(0);
    }
    std::size_t best = 0;
    int most = -1;
    for (std::size_t i = 0; i < root.actions.size(); ++i) {
        int v = 0;
        for (const auto& tv : visits) v += tv[i];
        result.visits[root.index[i]] = v;
        if (v > most) {  // ties: first in action order
            most = v;
            best = i;
        }
    }
    result.action = root.actions[best];
    for (const int n : done) result.iterations += n;
    return result;
}

void IsmctsPlayer::begin(PlayerId, const Deck& seat0, const Deck& seat1) { decks_ = {seat0, seat1}; }

Action IsmctsPlayer::choose(const GameState& view, const std::vector<Action>& actions) {
    iterations_ = 0;
    if (view.decision.kind == DecisionKind::Mulligan) {
        return mulligan_choice(view, actions, options_.mulligan_energy);
    }
    const IsmctsResult r = ismcts_search(view, actions, decks_[0], decks_[1], rng_.next(), options_);
    iterations_ = r.iterations;
    return r.action;
}

} // namespace buddy::riftbound::ai
