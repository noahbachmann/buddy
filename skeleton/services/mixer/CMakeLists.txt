add_executable(buddy-mixer main.cpp)
target_link_libraries(buddy-mixer PRIVATE buddy_shared)
install(TARGETS buddy-mixer RUNTIME DESTINATION bin COMPONENT mixer)
