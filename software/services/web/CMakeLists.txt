# Routes, state and access checks without MQTT: linked by the service and its tests (tests run a real server
# on a free port).
add_library(web_core STATIC
    access.cpp
    server.cpp
    state.cpp
)
target_include_directories(web_core PUBLIC .)
target_link_libraries(web_core PUBLIC
    buddy_shared
    httplib
    nlohmann_json::nlohmann_json
)

add_executable(buddy-web main.cpp)
target_link_libraries(buddy-web PRIVATE web_core buddy_shared)
install(TARGETS buddy-web RUNTIME DESTINATION bin COMPONENT web)
install(DIRECTORY www/ DESTINATION share/web COMPONENT web)

buddy_add_tests(web tests/test_access.cpp tests/test_server.cpp tests/test_state.cpp LIBS web_core)
