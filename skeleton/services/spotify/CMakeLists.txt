# Web API logic without libcurl: linked by the service and its tests (tests use a fake Http).
add_library(spotify_core STATIC
    auth.cpp
    http.cpp
    player.cpp
)
target_include_directories(spotify_core PUBLIC .)
target_link_libraries(spotify_core PUBLIC
    nlohmann_json::nlohmann_json
    OpenSSL::Crypto
)

add_executable(buddy-spotify
    main.cpp
    curl_http.cpp
)
target_link_libraries(buddy-spotify PRIVATE
    spotify_core
    buddy_shared
    CURL::libcurl
)
install(TARGETS buddy-spotify RUNTIME DESTINATION bin COMPONENT spotify)

buddy_add_tests(spotify tests/test_auth.cpp tests/test_player.cpp LIBS spotify_core)
