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

# game_core: the service's logic beyond the rules (5.5: engine moves → table commands, the soak's workload; 5.6: the
# session, SimDev, the public state, the command host).
add_library(game_core STATIC game_host.cpp game_store.cpp public_state.cpp session.cpp sim_dev.cpp table_moves.cpp)
target_include_directories(game_core PUBLIC .)
target_link_libraries(game_core PUBLIC riftbound buddy_table_protocol)

# game_sim: session games on an in-process sim table with the checks after every step, the soak and terminal play
# (#51): `buddy-game --sim` and the session tests. Not in game_core: the service talks to the table over MQTT.
add_library(game_sim STATIC sim_game.cpp sim_terminal.cpp)
target_include_directories(game_sim PUBLIC .)
target_link_libraries(game_sim PUBLIC game_core table_core)

add_executable(buddy-game main.cpp service.cpp)
target_link_libraries(buddy-game PRIVATE game_sim game_core riftbound buddy_shared)
install(TARGETS buddy-game RUNTIME DESTINATION bin COMPONENT game)
install(DIRECTORY decks/ DESTINATION share/game/decks COMPONENT game)

# Label riftbound: deploy/test.sh runs them only with --riftbound (the gate alone takes a while under ASan).
buddy_add_tests(game
    tests/test_abilities.cpp tests/test_actions.cpp tests/test_ai.cpp tests/test_card_db.cpp tests/test_chain.cpp tests/test_code.cpp tests/test_combat.cpp tests/test_deck.cpp tests/test_gate.cpp tests/test_hidden.cpp tests/test_pay.cpp tests/test_random.cpp
    tests/test_cards_vi.cpp tests/test_cards_zed.cpp tests/test_keywords.cpp tests/test_replay.cpp tests/test_setup.cpp tests/test_state.cpp tests/test_turn.cpp
    LIBS riftbound LABELS riftbound)
# Engine moves replayed on table's sim (#42): links table_core for the sim and the soak's stand-in.
buddy_add_tests(game-table tests/test_table_moves.cpp LIBS game_core table_core LABELS riftbound)
# Session games (#48–#51): Buddy's side on an in-process Table + Sim, Dev a SimDev.
buddy_add_tests(game-session tests/test_session.cpp tests/test_public_state.cpp tests/test_game_host.cpp tests/test_sim_soak.cpp tests/test_sim_terminal.cpp tests/test_journal.cpp tests/test_game_store.cpp LIBS game_sim LABELS riftbound)
