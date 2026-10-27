# Endpointing and level gate without ONNX: linked by the service and its tests.
add_library(ears_core STATIC endpoint.cpp)
target_include_directories(ears_core PUBLIC .)

add_executable(buddy-ears
    main.cpp
    audio_source.cpp
    onnx_model.cpp
    stt.cpp
    vad.cpp
    wake_word.cpp
)
target_link_libraries(buddy-ears PRIVATE
    ears_core
    buddy_shared
    onnxruntime::onnxruntime
    ALSA::ALSA
    whisper
)
# Keep the path to /opt/onnxruntime/lib in the installed binary (no ldconfig / LD_LIBRARY_PATH needed).
set_target_properties(buddy-ears PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
install(TARGETS buddy-ears RUNTIME DESTINATION bin COMPONENT ears)

buddy_add_tests(ears tests/test_endpoint.cpp tests/test_stt.cpp LIBS ears_core)
