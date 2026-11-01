# Riftbound (Phase 5, D21). riftbound: card data, decks, the rules engine and the AI (ai/, 5.4); pure, no
# MQTT, linked by the service and its tests.
add_library(riftbound STATIC
    riftbound/ai/arena.cpp
    riftbound/ai/eval.cpp
    riftbound/ai/greedy.cpp
    riftbound/ai/ismcts.cpp
    riftbound/ai/player.cpp
    riftbound/actions.cpp
    riftbound/card_db.cpp
    riftbound/cards/cards.cpp
    riftbound/cards/vi_unleashed.cpp
    riftbound/cards/zed_vendetta.cpp
    riftbound/code.cpp
    riftbound/deck.cpp
    riftbound/engine.cpp
    riftbound/hidden.cpp
    riftbound/pay.cpp
    riftbound/random_game.cpp
    riftbound/replay.cpp
    riftbound/rules.cpp
    riftbound/state.cpp
    riftbound/tokens.cpp
)
target_include_directories(riftbound PUBLIC .)
find_package(Threads REQUIRED)
target_link_libraries(riftbound PUBLIC nlohmann_json::nlohmann_json PRIVATE buddy_names Threads::Threads)

add_executable(buddy-game main.cpp)
target_link_libraries(buddy-game PRIVATE riftbound buddy_shared)
install(TARGETS buddy-game RUNTIME DESTINATION bin COMPONENT game)
install(DIRECTORY decks/ DESTINATION share/game/decks COMPONENT game)

# Label riftbound: deploy/test.sh runs them only with --riftbound (the gate alone takes a while under ASan).
buddy_add_tests(game
    tests/test_abilities.cpp tests/test_actions.cpp tests/test_ai.cpp tests/test_card_db.cpp tests/test_chain.cpp tests/test_code.cpp tests/test_combat.cpp tests/test_deck.cpp tests/test_gate.cpp tests/test_hidden.cpp tests/test_pay.cpp tests/test_random.cpp
    tests/test_cards_vi.cpp tests/test_cards_zed.cpp tests/test_keywords.cpp tests/test_replay.cpp tests/test_setup.cpp tests/test_state.cpp tests/test_turn.cpp
    LIBS riftbound LABELS riftbound)
