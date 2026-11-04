# Riftbound table (5.5): Buddy's physical cards. table_core = tracker (belief + pile procedures), the command layer
# and the sim backend; no MQTT, linked by the service, its tests and game's table-move tests.
add_library(table_core STATIC
    sim.cpp
    table.cpp
    tracker.cpp
)
target_include_directories(table_core PUBLIC .)
target_link_libraries(table_core PUBLIC buddy_table_protocol)

buddy_add_tests(table tests/test_tracker.cpp LIBS table_core)
