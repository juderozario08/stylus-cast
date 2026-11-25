cmake_minimum_required(VERSION 3.16)
project(PenBridgeServer VERSION 1.1.0 LANGUAGES CXX)

if(NOT APPLE)
    message(FATAL_ERROR "PenBridge server requires macOS (CoreGraphics).")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # for clangd and other tooling

add_library(penbridge STATIC
    src/beacon.cpp
    src/config.cpp
    src/display.cpp
    src/injector.cpp
    src/jitter_buffer.cpp
    src/pen_sample.cpp
    src/pen_state.cpp
    src/protocol.cpp
    src/realtime.cpp
    src/server.cpp
    src/stream_filter.cpp
    src/stroke_smoother.cpp
    src/tablet_events.cpp
    src/trace.cpp
)
target_include_directories(penbridge PUBLIC include)
target_compile_options(penbridge PUBLIC -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
target_link_libraries(penbridge PUBLIC "-framework ApplicationServices" "-framework CoreFoundation")

add_executable(penbridge-server src/main.cpp)
target_link_libraries(penbridge-server PRIVATE penbridge)

enable_testing()
add_executable(penbridge-tests
    tests/main.cpp
    tests/test_jitter_buffer.cpp
    tests/test_pen_state.cpp
    tests/test_protocol.cpp
    tests/test_stream_filter.cpp
    tests/test_stroke_smoother.cpp
    tests/test_tablet_events.cpp
)
target_link_libraries(penbridge-tests PRIVATE penbridge)
add_test(NAME PenBridgeTests COMMAND penbridge-tests)
