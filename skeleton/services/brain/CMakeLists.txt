add_executable(buddy-brain
    main.cpp
    rules.cpp
)
target_link_libraries(buddy-brain PRIVATE buddy_shared)
install(TARGETS buddy-brain RUNTIME DESTINATION bin)
