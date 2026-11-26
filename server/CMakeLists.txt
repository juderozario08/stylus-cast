cmake_minimum_required(VERSION 3.16)
project(StylusCastServer VERSION 1.2.0 LANGUAGES CXX)

if(NOT WIN32 AND NOT APPLE)
    message(FATAL_ERROR "Stylus-Cast server supports Windows and macOS.")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(STYLUS_CAST_COMMON_SOURCES
    src/adb_bridge.cpp
    src/config.cpp
    src/display.cpp
    src/jitter_buffer.cpp
    src/pen_sample.cpp
    src/pen_state.cpp
    src/protocol.cpp
    src/server.cpp
    src/stream_filter.cpp
    src/stroke_smoother.cpp
    src/trace.cpp
)

if(WIN32)
    set(STYLUS_CAST_PLATFORM_SOURCES
        src/windows/beacon.cpp
        src/windows/display.cpp
        src/windows/injector.cpp
        src/windows/realtime.cpp
        src/windows/tablet_events.cpp
    )
elseif(APPLE)
    set(STYLUS_CAST_PLATFORM_SOURCES
        src/macos/beacon.cpp
        src/macos/display.cpp
        src/macos/injector.cpp
        src/macos/realtime.cpp
        src/macos/tablet_events.cpp
    )
endif()

add_library(stylus-cast STATIC
    ${STYLUS_CAST_COMMON_SOURCES}
    ${STYLUS_CAST_PLATFORM_SOURCES}
)

target_include_directories(stylus-cast PUBLIC include)

if(WIN32)
    target_compile_definitions(stylus-cast PUBLIC
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0A00
        NTDDI_VERSION=NTDDI_WIN10_RS5
    )
    if(MSVC)
        target_compile_definitions(stylus-cast PUBLIC _CRT_SECURE_NO_WARNINGS)
        target_compile_options(stylus-cast PUBLIC /W4 /permissive- /utf-8)
    else()
        target_compile_options(stylus-cast PUBLIC -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    endif()
    target_link_libraries(stylus-cast PUBLIC ws2_32 iphlpapi user32 winmm)
elseif(APPLE)
    target_compile_options(stylus-cast PUBLIC -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    target_link_libraries(stylus-cast PUBLIC
        "-framework ApplicationServices"
        "-framework CoreFoundation"
        pthread
    )
endif()

add_executable(stylus-cast-server src/main.cpp)
target_link_libraries(stylus-cast-server PRIVATE stylus-cast)

add_executable(stylus-cast-tests
    tests/main.cpp
    tests/test_jitter_buffer.cpp
    tests/test_pen_state.cpp
    tests/test_protocol.cpp
    tests/test_stream_filter.cpp
    tests/test_stroke_smoother.cpp
    tests/test_tablet_events.cpp
    tests/test_server.cpp
)
target_link_libraries(stylus-cast-tests PRIVATE stylus-cast)

enable_testing()
add_test(NAME stylus-cast-tests COMMAND stylus-cast-tests)
