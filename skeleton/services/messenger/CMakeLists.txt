# Chat logic, watcher and Bot API without libcurl: linked by the service and its tests (tests use a fake Http).
add_library(messenger_core STATIC
    chat.cpp
    command.cpp
    store.cpp
    watch.cpp
)
target_include_directories(messenger_core PUBLIC .)
target_link_libraries(messenger_core PUBLIC
    buddy_http
    nlohmann_json::nlohmann_json
)

buddy_add_tests(messenger tests/test_chat.cpp tests/test_command.cpp tests/test_store.cpp tests/test_watch.cpp
    LIBS messenger_core)
