cmake_minimum_required(VERSION 3.25)
project(AGRA VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Target Windows 10/11 (0x0A00)
add_compile_definitions(
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

# Compiler warnings and options
if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8)
else()
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unknown-pragmas
    )
endif()

# Global output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Core AGRA Library
file(GLOB_RECURSE AGRA_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/discovery/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/game/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/monitoring/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/analysis/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/allocator/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/windows/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/safety/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/policy/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/session/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/simulation/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/benchmark/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp
)

add_library(agra_core STATIC ${AGRA_CORE_SOURCES})
target_include_directories(agra_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(agra_core PUBLIC
    kernel32
    user32
    advapi32
    psapi
    pdh
    powrprof
    shlwapi
    comctl32
)

# CLI Application
add_executable(agra ${CMAKE_CURRENT_SOURCE_DIR}/src/main_cli.cpp)
target_link_libraries(agra PRIVATE agra_core)

# Native Win32 GUI Application
add_executable(agra_gui WIN32 ${CMAKE_CURRENT_SOURCE_DIR}/src/main_gui.cpp)
target_link_libraries(agra_gui PRIVATE agra_core)

# Testing
enable_testing()
add_subdirectory(tests)
