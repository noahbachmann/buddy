cmake_minimum_required(VERSION 3.28)
project(buddy LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wall -Wextra -Wpedantic)

# System packages (Debian 13): libpaho-mqtt-dev, libpaho-mqttpp-dev, libssl-dev, nlohmann-json3-dev, libasound2-dev,
#   libcurl4-openssl-dev
# Not from apt: ONNX Runtime (/opt/onnxruntime), libpiper (/opt/libpiper) and whisper.cpp, see third_party/
find_package(PahoMqttCpp REQUIRED)
find_package(nlohmann_json 3 REQUIRED)
find_package(ALSA REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

add_subdirectory(third_party)
add_subdirectory(shared)
add_subdirectory(services/ping)
add_subdirectory(services/pong)
add_subdirectory(services/ears)
add_subdirectory(services/brain)
add_subdirectory(services/spotify)
add_subdirectory(services/voice)
add_subdirectory(services/mixer)
