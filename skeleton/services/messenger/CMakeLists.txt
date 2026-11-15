# Chat logic, watcher and Bot API without libcurl: linked by the service and its tests (tests use a fake Http).
add_library(messenger_core STATIC
    chat.cpp
    command.cpp
    store.cpp
    telegram.cpp
    watch.cpp
)
target_include_directories(messenger_core PUBLIC .)
target_link_libraries(messenger_core PUBLIC
    buddy_http
    nlohmann_json::nlohmann_json
)

add_executable(buddy-messenger main.cpp)
target_link_libraries(buddy-messenger PRIVATE
    messenger_core
    buddy_shared
    buddy_curl_http
)
install(TARGETS buddy-messenger RUNTIME DESTINATION bin COMPONENT messenger)

buddy_add_tests(messenger tests/test_chat.cpp tests/test_command.cpp tests/test_store.cpp tests/test_telegram.cpp
    tests/test_watch.cpp
    LIBS messenger_core)
