# Metric parsers and payload assembly without I/O: linked by the service and its tests.
add_library(system_core STATIC
    metrics.cpp
)
target_include_directories(system_core PUBLIC .)
target_link_libraries(system_core PUBLIC
    nlohmann_json::nlohmann_json
)

add_executable(buddy-system main.cpp)
target_link_libraries(buddy-system PRIVATE
    system_core
    buddy_shared
)
install(TARGETS buddy-system RUNTIME DESTINATION bin COMPONENT system)

buddy_add_tests(system tests/test_metrics.cpp LIBS system_core)
