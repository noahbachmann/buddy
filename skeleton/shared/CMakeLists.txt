add_library(buddy_shared STATIC
    src/config.cpp
    src/mqtt.cpp
    src/topics.cpp
)
target_include_directories(buddy_shared PUBLIC include)
target_link_libraries(buddy_shared PUBLIC
    PahoMqttCpp::paho-mqttpp3
    nlohmann_json::nlohmann_json
)
