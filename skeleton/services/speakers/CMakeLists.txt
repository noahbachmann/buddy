# Routing logic and tool-output parsers without I/O: linked by the service and its tests.
add_library(speakers_core STATIC
    parse.cpp
    router.cpp
)
target_include_directories(speakers_core PUBLIC .)
target_link_libraries(speakers_core PUBLIC
    buddy_names
    nlohmann_json::nlohmann_json
)

add_executable(buddy-speakers main.cpp)
target_link_libraries(buddy-speakers PRIVATE
    speakers_core
    buddy_shared
)
install(TARGETS buddy-speakers RUNTIME DESTINATION bin COMPONENT speakers)

buddy_add_tests(speakers tests/test_parse.cpp tests/test_router.cpp LIBS speakers_core)
