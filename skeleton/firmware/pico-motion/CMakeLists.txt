# Firmware for the Pico 2 that drives every Buddy servo (D17): head, later hand and arm. Own CMake project: the Pico
# SDK picks the ARM toolchain before project(), so this can't be an add_subdirectory of skeleton/.
# Built and flashed on the Pi: deploy/deploy.sh --firmware.
cmake_minimum_required(VERSION 3.28)

set(PICO_BOARD pico2)
set(PICO_PLATFORM rp2350-arm-s)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Pinned tarballs + hashes like whisper.cpp (D9), not git clones: TinyUSB's full history is huge. TinyUSB = the commit
# SDK 2.3.1's lib/tinyusb submodule points at. SDK version: keep = PICO_SDK_VERSION in deploy/pi/setup.sh.
# SOURCE_SUBDIR none: download only, pico_sdk_init.cmake includes the SDK itself.
include(FetchContent)
FetchContent_Declare(pico_sdk
    URL https://github.com/raspberrypi/pico-sdk/archive/refs/tags/2.3.1.tar.gz
    URL_HASH SHA256=38d315b7091985134721cd3bb6a9656f0ef8462ec8dc7b62d5886bc7a9d275a2
    SOURCE_SUBDIR none
)
FetchContent_Declare(tinyusb
    URL https://github.com/hathach/tinyusb/archive/86ad6e56c1700e85f1c5678607a762cfe3aa2f47.tar.gz
    URL_HASH SHA256=9f43fd2a745e2543a42ac79b99774c24155e6ef1e0d2ae3cb16ed4ffacf5c517
    SOURCE_SUBDIR none
)
FetchContent_MakeAvailable(pico_sdk tinyusb)
set(PICO_SDK_PATH ${pico_sdk_SOURCE_DIR})
set(PICO_TINYUSB_PATH ${tinyusb_SOURCE_DIR})

# Without it the SDK downloads and builds its own picotool.
set(picotool_DIR /opt/picotool/lib/cmake/picotool CACHE PATH "picotool from deploy/pi/setup.sh pico")
if(NOT EXISTS ${picotool_DIR}/picotoolConfig.cmake)
    message(FATAL_ERROR "picotool not found in ${picotool_DIR}: run deploy/pi/setup.sh pico")
endif()

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)
project(buddy_motion VERSION 0.1.0 LANGUAGES C CXX ASM)
pico_sdk_init()

add_executable(buddy-motion main.cpp servo.cpp)
target_include_directories(buddy-motion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/include)
# Our files only: the SDK's sources compile inside this target and aren't -Wpedantic clean.
set_source_files_properties(main.cpp servo.cpp PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic")
target_compile_definitions(buddy-motion PRIVATE
    BUDDY_MOTION_VERSION="${PROJECT_VERSION}"
    USBD_PRODUCT="Buddy motion"               # udev rule → /dev/buddy-pico
    PICO_STDIO_USB_STDOUT_TIMEOUT_US=20000    # port open but nobody reading: drop output, don't starve the watchdog
)
target_link_libraries(buddy-motion PRIVATE pico_stdlib hardware_pwm hardware_watchdog hardware_clocks)
pico_enable_stdio_usb(buddy-motion 1)
pico_enable_stdio_uart(buddy-motion 0)
pico_add_extra_outputs(buddy-motion)  # buddy-motion.uf2
