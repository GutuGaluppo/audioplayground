cmake_minimum_required(VERSION 3.25)

project(AudioPlayground
    VERSION 0.1.0
    DESCRIPTION "Desktop-first creative audio playground"
    LANGUAGES C CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use a preset: cmake --preset dev")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")
endif()

option(AP_BUILD_TESTS "Build unit tests" ON)
option(AP_BUILD_DESKTOP "Build the desktop application" ON)
option(AP_WARNINGS_AS_ERRORS "Treat compiler warnings as errors on first-party targets" ON)
set(AP_SANITIZERS "" CACHE STRING "Semicolon-separated sanitizers for first-party code (address;undefined | thread | realtime)")

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(ProjectOptions)
include(Dependencies)

add_subdirectory(core)

if(AP_BUILD_DESKTOP)
    add_subdirectory(apps/desktop)
endif()

if(AP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
