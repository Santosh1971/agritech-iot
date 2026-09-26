cmake_minimum_required(VERSION 3.22)
project(flashertester LANGUAGES C)

# esp-serial-flasher's own CMakeLists creates target `flasher`. PORT=USER_DEFINED
# skips every bundled port (they're for ESP-IDF/STM32/Pi Pico/Linux hosts, not
# Android) so we can attach our own JNI port below — see its CMakeLists.txt
# around the PORT STREQUAL "USER_DEFINED" branch.
set(PORT USER_DEFINED)
add_subdirectory(esp-serial-flasher)

target_sources(flasher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/android_port.c
)

# Pure C — no C++ standard library needed, so ANDROID_STL=none (set in
# app/build.gradle.kts) keeps libflashertester.so from pulling in libc++_shared.
add_library(flashertester SHARED
    jni_bridge.c
)

find_library(log-lib log)

target_link_libraries(flashertester PRIVATE flasher ${log-lib})
