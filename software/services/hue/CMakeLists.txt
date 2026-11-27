# Hue's root CA (public, from Hue's developer docs; verified against the bridge's certificate) compiled in:
# no file to install or configure.
file(READ root-bridge.pem HUE_ROOT_CA)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/root-bridge.pem)
configure_file(hue_ca.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/include/hue_ca.hpp @ONLY)

# Bridge API, model and command logic without libcurl: linked by the service and its tests (tests use a fake Http).
add_library(hue_core STATIC
    bridge.cpp
    home.cpp
    lights.cpp
    sse.cpp
    throttle.cpp
)
target_include_directories(hue_core PUBLIC . PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)
target_link_libraries(hue_core PUBLIC
    buddy_http
    buddy_names
    nlohmann_json::nlohmann_json
)

add_executable(buddy-hue main.cpp)
target_link_libraries(buddy-hue PRIVATE
    hue_core
    buddy_shared
    buddy_curl_http
)
install(TARGETS buddy-hue RUNTIME DESTINATION bin COMPONENT hue)

buddy_add_tests(hue tests/test_bridge.cpp tests/test_home.cpp tests/test_lights.cpp tests/test_sse.cpp tests/test_throttle.cpp
    tests/test_dev_home.cpp
    LIBS hue_core)
