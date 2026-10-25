# MQTT topic names: topics.json is the single source for C++ and Python (D15). The header is generated at
# configure time; editing topics.json re-runs CMake.
set(TOPICS_JSON ${CMAKE_CURRENT_SOURCE_DIR}/topics.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${TOPICS_JSON})
file(READ ${TOPICS_JSON} topics_json)
string(JSON topic_count LENGTH "${topics_json}" topics)
math(EXPR topic_last "${topic_count} - 1")
set(TOPIC_CONSTANTS "")
foreach(i RANGE ${topic_last})
    string(JSON name GET "${topics_json}" topics ${i} name)
    string(JSON topic GET "${topics_json}" topics ${i} topic)
    string(JSON kind GET "${topics_json}" topics ${i} kind)
    string(JSON retained ERROR_VARIABLE no_retained GET "${topics_json}" topics ${i} retained)
    string(JSON planned ERROR_VARIABLE no_planned GET "${topics_json}" topics ${i} planned)
    string(JSON field_count ERROR_VARIABLE no_payload LENGTH "${topics_json}" topics ${i} payload)
    set(comment "${kind}")
    if(retained AND NOT no_retained)
        string(APPEND comment ", retained")
    endif()
    if(planned AND NOT no_planned)
        string(APPEND comment " (planned)")
    endif()
    if(NOT no_payload)
        set(fields "")
        math(EXPR field_last "${field_count} - 1")
        foreach(f RANGE ${field_last})
            string(JSON field GET "${topics_json}" topics ${i} payload ${f})
            list(APPEND fields "${field}")
        endforeach()
        list(JOIN fields ", " fields)
        string(APPEND comment " {${fields}}")
    endif()
    string(APPEND TOPIC_CONSTANTS "inline constexpr std::string_view ${name} = \"${topic}\";  // ${comment}\n")
endforeach()
configure_file(topics.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/include/buddy/topics.hpp @ONLY)

add_library(buddy_shared STATIC
    src/config.cpp
    src/mqtt.cpp
    src/systemd.cpp
    src/topics.cpp
)
target_include_directories(buddy_shared PUBLIC include ${CMAKE_CURRENT_BINARY_DIR}/include)
target_link_libraries(buddy_shared PUBLIC
    PahoMqttCpp::paho-mqttpp3
    nlohmann_json::nlohmann_json
)
