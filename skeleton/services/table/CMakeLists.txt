# Riftbound table (5.5): Buddy's physical cards. table_core = tracker (belief + pile procedures), the command layer
# and the sim backend; no MQTT, linked by the service, its tests and game's table-move tests.
add_library(table_core STATIC
    sim.cpp
    soak.cpp
    table.cpp
    tracker.cpp
)
target_include_directories(table_core PUBLIC .)
target_link_libraries(table_core PUBLIC buddy_table_protocol)

buddy_add_tests(table tests/test_faults.cpp tests/test_tracker.cpp LIBS table_core)

add_executable(buddy-table main.cpp)
target_link_libraries(buddy-table PRIVATE table_core buddy_shared)
install(TARGETS buddy-table RUNTIME DESTINATION bin COMPONENT table)
