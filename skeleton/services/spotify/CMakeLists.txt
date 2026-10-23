add_executable(buddy-spotify
    main.cpp
    auth.cpp
    http.cpp
    player.cpp
)
target_link_libraries(buddy-spotify PRIVATE
    buddy_shared
    CURL::libcurl
    OpenSSL::Crypto
)
install(TARGETS buddy-spotify RUNTIME DESTINATION bin COMPONENT spotify)
