#include "cards.hpp"

namespace buddy::riftbound::cards {

const CardRules* rules_for(std::string_view name) {
    for (const auto list : {vi_unleashed(), zed_vendetta()}) {
        for (const Entry& e : list) {
            if (e.name == name) return e.rules;
        }
    }
    return nullptr;
}

} // namespace buddy::riftbound::cards
