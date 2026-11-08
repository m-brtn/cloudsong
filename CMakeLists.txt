cmake_minimum_required(VERSION 3.28)
# macOS 14 is the engine's minimum (its metallibs); the iOS presets set 26.0.
if(CMAKE_HOST_APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 14.0 CACHE STRING "Minimum macOS version" FORCE)
endif()
project(cloudsong VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The engine at a pinned commit. Working on the engine at the same time: configure with
# -DFETCHCONTENT_SOURCE_DIR_QUENCH=<engine checkout>.
include(FetchContent)
# main after v0.11.0: the camera above the clouds
FetchContent_Declare(quench
    GIT_REPOSITORY https://github.com/m-brtn/Quench.git
    GIT_TAG 4a22a0f9b7783a088771c59679f91dbb8f9c501a)
FetchContent_MakeAvailable(quench)

# The host-tools preset builds only the engine's shader compiler and asset cooker (for the iOS builds).
if(QUENCH_HEADLESS)
    return()
endif()

quench_add_game(cloudsong
    NAME "Cloudsong"
    BUNDLE_ID org.quench.cloudsong
    ICON icon/icon.png
    ASSETS assets
    ORIENTATIONS landscape
    SOURCES src/Game.cpp src/Character.cpp src/World.cpp src/Glow.cpp src/Player.cpp)
target_include_directories(cloudsong PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(cloudsong PRIVATE -Wall -Wextra)
endif()
target_link_libraries(cloudsong PRIVATE quench_render quench_physics quench_animation)
quench_add_shaders(cloudsong NAME world SOURCE shaders/world.hlsl
    ENTRIES surface_vs:vertex surface_fs:fragment surface_basic_fs:fragment)
quench_add_shaders(cloudsong NAME foliage SOURCE shaders/foliage.hlsl
    ENTRIES surface_vs:vertex surface_fs:fragment surface_basic_fs:fragment surface_shadow_vs:vertex)
quench_add_shaders(cloudsong NAME glow SOURCE shaders/glow.hlsl ENTRIES glow_vs:vertex glow_fs:fragment)

if(NOT CMAKE_CROSSCOMPILING)
    enable_testing()
    add_test(NAME smoke COMMAND cloudsong --window 640x360 --script ${CMAKE_CURRENT_SOURCE_DIR}/scenarios/smoke.txt)
    set_tests_properties(smoke PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL line")
endif()
