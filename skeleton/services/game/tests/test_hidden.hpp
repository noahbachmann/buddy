#pragma once
// Views and determinizations checked along random games (test_hidden.cpp, test_abilities.cpp's ability deck).

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>

#include "riftbound/engine.hpp"
#include "riftbound/hidden.hpp"
#include "riftbound/random_game.hpp"

namespace buddy::riftbound::test {

// Cards per owner: a determinization keeps every count, only where the hidden ones are differs (zone sizes are part
// of the view).
inline std::map<std::pair<PlayerId, const Card*>, int> card_counts(const GameState& s) {
    std::map<std::pair<PlayerId, const Card*>, int> n;
    for (const Object& o : s.objects) ++n[{o.owner, o.card}];
    return n;
}

// For both players at state `s`: the view hides exactly what the player doesn't know, a determinization of it is a
// valid state with the same cards in every zone, the same view, and (for the deciding player) the same legal actions;
// with `play_on` it also runs on for 30 random actions under GameChecks. Empty = fine.
inline std::string check_view(const GameState& s, const Deck& seat0, const Deck& seat1, Rng& rng, bool play_on) {
    for (PlayerId p = 0; p < player_count; ++p) {
        const std::string who = "player " + std::to_string(p) + ": ";
        const GameState v = view(s, p);
        for (ObjId id = 0; id < s.objects.size(); ++id) {
            const Object& o = s.obj(id);
            if ((v.obj(id).card == hidden_card()) == static_cast<bool>(o.known & 1u << p)) {
                return who + o.card->name + " in the " + std::string(to_string(o.zone)) + " wrongly hidden or shown";
            }
            if (o.zone == Zone::Hand && o.owner == p && v.obj(id).card != o.card) return who + "own hand hidden";
            if (is_public(o.zone) && v.obj(id).card != o.card) return who + "public card hidden";
        }
        GameState d;
        try {
            d = determinize(v, seat0, seat1, rng);
        } catch (const std::logic_error& e) {
            return who + e.what();
        }
        if (auto problems = check_invariants(d); !problems.empty()) return who + "determinized: " + problems.front();
        if (card_counts(d) != card_counts(s)) return who + "determinized: other cards";
        if (!(view(d, p) == v)) return who + "the determinization's view differs";
        if (p == s.decision.player && legal_actions(d) != legal_actions(s)) {
            return who + "legal actions depend on hidden cards";
        }
        if (!play_on) continue;
        GameChecks checks(d);
        for (int i = 0; i < 30 && !d.winner; ++i) {
            const auto actions = legal_actions(d);
            const Action a = actions.at(rng.below(actions.size()));
            try {
                apply(d, a);
            } catch (const std::exception& e) {
                return who + "determinized game, " + describe(d, a) + ": " + e.what();
            }
            if (auto problem = checks.check(d); !problem.empty()) return who + "determinized game: " + problem;
        }
    }
    return {};
}

// A random game (no checks of its own beyond play_random's) with check_view after every action; every 25th also
// plays on. Returns the first problem.
inline std::string views_along_a_game(const Deck& seat0, const Deck& seat1, std::uint64_t seed, bool vanilla) {
    GameState s = new_game(seat0, seat1, {.seed = seed, .vanilla = vanilla});
    Rng pick(seed), sample(seed + 1000);
    for (int n = 0; !s.winner && n < 5000; ++n) {
        if (auto problem = check_view(s, seat0, seat1, sample, n % 25 == 0); !problem.empty()) {
            return "seed " + std::to_string(seed) + ", action " + std::to_string(n) + ", " + problem;
        }
        const auto actions = legal_actions(s);
        apply(s, actions.at(pick.below(actions.size())));
    }
    return s.winner ? std::string{} : "seed " + std::to_string(seed) + ": no winner";
}

} // namespace buddy::riftbound::test
