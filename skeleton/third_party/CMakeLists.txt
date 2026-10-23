# Libraries that Debian 13 doesn't package. Built from pinned source tarballs (downloaded once per build dir).

# Third-party code gets its own warning flags, not ours (-Wpedantic floods the build log).
set_directory_properties(PROPERTIES COMPILE_OPTIONS "")

include(FetchContent)

# ONNX Runtime (wake word + VAD, steps 1.6/1.7): Microsoft's prebuilt release, unpacked by
# deploy/setup-pi.sh into /opt/onnxruntime. Not Debian's libonnxruntime-dev: that build registers
# every ONNX schema twice and prints ~570 "Schema error" lines on each start (journald, microSD).
# The release's own CMake config points at wrong paths, hence the manual imported target.
set(ONNXRUNTIME_ROOT /opt/onnxruntime CACHE PATH "Unpacked onnxruntime-linux-<arch>-<version>.tgz")
find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h PATHS ${ONNXRUNTIME_ROOT}/include NO_DEFAULT_PATH REQUIRED)
find_library(ONNXRUNTIME_LIBRARY onnxruntime PATHS ${ONNXRUNTIME_ROOT}/lib NO_DEFAULT_PATH REQUIRED)
add_library(onnxruntime::onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(onnxruntime::onnxruntime PROPERTIES
    IMPORTED_LOCATION ${ONNXRUNTIME_LIBRARY}
    INTERFACE_INCLUDE_DIRECTORIES ${ONNXRUNTIME_INCLUDE_DIR}
)

# libpiper (text-to-speech, step 1.10): built from source by deploy/setup-pi.sh into /opt/libpiper, against
# the ONNX Runtime above. Not FetchContent: its CMakeLists reads ${CMAKE_SOURCE_DIR}/../setup.py, so it only
# builds as its own top-level project. Its espeak-ng data lands in /opt/libpiper/share/espeak-ng-data.
set(LIBPIPER_ROOT /opt/libpiper CACHE PATH "libpiper install prefix")
find_path(LIBPIPER_INCLUDE_DIR piper.h PATHS ${LIBPIPER_ROOT}/include NO_DEFAULT_PATH REQUIRED)
find_library(LIBPIPER_LIBRARY piper PATHS ${LIBPIPER_ROOT}/lib NO_DEFAULT_PATH REQUIRED)
add_library(piper::piper SHARED IMPORTED GLOBAL)
set_target_properties(piper::piper PROPERTIES
    IMPORTED_LOCATION ${LIBPIPER_LIBRARY}
    INTERFACE_INCLUDE_DIRECTORIES ${LIBPIPER_INCLUDE_DIR}
)

# whisper.cpp (speech-to-text, step 1.7). Static, CPU only; GGML_NATIVE tunes it for the CPU it is
# built on, which is the Pi itself (D6).
# CMP0077 NEW: whisper's option() calls must respect the variables set here (its CMakeLists is old-style).
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(BUILD_SHARED_LIBS OFF)
set(WHISPER_BUILD_TESTS OFF)
set(WHISPER_BUILD_EXAMPLES OFF)
set(WHISPER_BUILD_SERVER OFF)
set(GGML_NATIVE ON)
FetchContent_Declare(whisper
    URL https://github.com/ggml-org/whisper.cpp/archive/refs/tags/v1.9.5.tar.gz
    URL_HASH SHA256=ff1a9053feb509ff9d7729703355541ae9690073a6b1c40eb692c962e0dc1720
    EXCLUDE_FROM_ALL  # keeps whisper's own install() rules out of ~/buddy
)
FetchContent_MakeAvailable(whisper)

# Even in CLion's Debug build, whisper must be optimized: unoptimized it is ~10× slower.
foreach(t whisper ggml ggml-base ggml-cpu)
    if(TARGET ${t})
        target_compile_options(${t} PRIVATE $<$<CONFIG:Debug>:-O2>)
    endif()
endforeach()
