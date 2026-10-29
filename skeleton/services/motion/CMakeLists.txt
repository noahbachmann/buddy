# Joints, gestures, command parsing and the serial link without MQTT: linked by the service and its tests
# (the link is tested against a fake Pico on a pseudo-terminal).
add_library(motion_core STATIC
    command.cpp
    gestures.cpp
    joints.cpp
    serial_link.cpp
)
target_include_directories(motion_core PUBLIC . ${PROJECT_SOURCE_DIR}/shared/include)
target_link_libraries(motion_core PUBLIC nlohmann_json::nlohmann_json)

add_executable(buddy-motion main.cpp)
target_link_libraries(buddy-motion PRIVATE motion_core buddy_shared)
install(TARGETS buddy-motion RUNTIME DESTINATION bin COMPONENT motion)

buddy_add_tests(motion tests/test_command.cpp tests/test_gestures.cpp tests/test_joints.cpp tests/test_serial_link.cpp
    LIBS motion_core util)
