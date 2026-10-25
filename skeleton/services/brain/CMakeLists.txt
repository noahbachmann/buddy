add_executable(buddy-brain
    main.cpp
    rules.cpp
)
target_link_libraries(buddy-brain PRIVATE buddy_shared)
install(TARGETS buddy-brain RUNTIME DESTINATION bin COMPONENT brain)

# ctest: tools/voice/commands.tsv is the phrasing brain must understand. Not installed, not built by deploy.sh.
add_executable(buddy-brain-test test_corpus.cpp rules.cpp)
add_test(NAME brain_corpus COMMAND buddy-brain-test ${PROJECT_SOURCE_DIR}/tools/voice/commands.tsv)
