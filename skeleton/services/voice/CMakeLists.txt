add_executable(buddy-voice
    main.cpp
    audio_sink.cpp
    tts.cpp
)
target_link_libraries(buddy-voice PRIVATE
    buddy_shared
    piper::piper
    ALSA::ALSA
)
# Keep the path to /opt/libpiper/lib in the installed binary (no ldconfig / LD_LIBRARY_PATH needed).
set_target_properties(buddy-voice PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
install(TARGETS buddy-voice RUNTIME DESTINATION bin)
