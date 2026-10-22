cmake_minimum_required(VERSION 3.25)
project(buddy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wall -Wextra -Wpedantic)

# System packages (Debian 13): libpaho-mqttpp-dev, nlohmann-json3-dev
find_package(PahoMqttCpp REQUIRED)
find_package(nlohmann_json 3 REQUIRED)

add_subdirectory(shared)
add_subdirectory(services/ping)
add_subdirectory(services/pong)
