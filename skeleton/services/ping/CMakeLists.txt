add_executable(buddy-ping main.cpp)
target_link_libraries(buddy-ping PRIVATE buddy_shared)
install(TARGETS buddy-ping RUNTIME DESTINATION bin COMPONENT ping)
