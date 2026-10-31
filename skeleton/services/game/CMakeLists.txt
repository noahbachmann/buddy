# Riftbound (Phase 5, D21). riftbound: card data, decks and the rules engine (5.2, growing), later the AI; pure, no
# MQTT, linked by the service and its tests.
add_library(riftbound STATIC
    riftbound/actions.cpp
    riftbound/card_db.cpp
    riftbound/code.cpp
    riftbound/deck.cpp
    riftbound/engine.cpp
    riftbound/pay.cpp
    riftbound/random_game.cpp
    riftbound/rules.cpp
    riftbound/state.cpp
)
target_include_directories(riftbound PUBLIC .)
target_link_libraries(riftbound PUBLIC nlohmann_json::nlohmann_json PRIVATE buddy_names)

add_executable(buddy-game main.cpp)
target_link_libraries(buddy-game PRIVATE riftbound buddy_shared)
install(TARGETS buddy-game RUNTIME DESTINATION bin COMPONENT game)
install(DIRECTORY decks/ DESTINATION share/game/decks COMPONENT game)

buddy_add_tests(game
    tests/test_abilities.cpp tests/test_card_db.cpp tests/test_chain.cpp tests/test_code.cpp tests/test_deck.cpp tests/test_pay.cpp tests/test_random.cpp
    tests/test_setup.cpp tests/test_state.cpp tests/test_turn.cpp
    LIBS riftbound)
