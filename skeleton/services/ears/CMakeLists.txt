add_executable(buddy-ears
    main.cpp
    audio_source.cpp
    onnx_model.cpp
    stt.cpp
    vad.cpp
    wake_word.cpp
)
target_link_libraries(buddy-ears PRIVATE
    buddy_shared
    onnxruntime::onnxruntime
    ALSA::ALSA
    whisper
)
# Keep the path to /opt/onnxruntime/lib in the installed binary (no ldconfig / LD_LIBRARY_PATH needed).
set_target_properties(buddy-ears PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
install(TARGETS buddy-ears RUNTIME DESTINATION bin)
