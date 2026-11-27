# Intent rules without MQTT: linked by the service and its tests.
add_library(brain_core STATIC rules.cpp)
target_include_directories(brain_core PUBLIC .)

add_executable(buddy-brain main.cpp)
target_link_libraries(buddy-brain PRIVATE brain_core buddy_shared)
install(TARGETS buddy-brain RUNTIME DESTINATION bin COMPONENT brain)

buddy_add_tests(brain tests/test_corpus.cpp tests/test_rules.cpp LIBS brain_core)
