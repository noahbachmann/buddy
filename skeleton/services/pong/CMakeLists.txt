add_executable(buddy-pong main.cpp)
target_link_libraries(buddy-pong PRIVATE buddy_shared)
install(TARGETS buddy-pong RUNTIME DESTINATION bin COMPONENT pong)
