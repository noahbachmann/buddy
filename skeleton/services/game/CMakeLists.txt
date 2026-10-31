# Riftbound (Phase 5, D21). riftbound: card data and decks so far, later rules engine and AI; pure, no MQTT, linked by
# the service and its tests.
add_library(riftbound STATIC
    riftbound/card_db.cpp
    riftbound/code.cpp
    riftbound/deck.cpp
)
target_include_directories(riftbound PUBLIC .)
target_link_libraries(riftbound PUBLIC nlohmann_json::nlohmann_json PRIVATE buddy_names)

add_executable(buddy-game main.cpp)
target_link_libraries(buddy-game PRIVATE riftbound buddy_shared)
install(TARGETS buddy-game RUNTIME DESTINATION bin COMPONENT game)
install(DIRECTORY decks/ DESTINATION share/game/decks COMPONENT game)

buddy_add_tests(game tests/test_card_db.cpp tests/test_code.cpp tests/test_deck.cpp LIBS riftbound)
