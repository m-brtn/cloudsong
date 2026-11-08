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
# main after v0.11.0: the camera above the clouds, quench_weather_sounds() in games, view rays far from the origin
FetchContent_Declare(quench
    GIT_REPOSITORY https://github.com/m-brtn/Quench.git
    GIT_TAG a66401aecc03bf9048f222d5c837d6549f8f383f)
FetchContent_MakeAvailable(quench)

# The host-tools preset builds only the engine's shader compiler and asset cooker (for the iOS builds).
if(QUENCH_HEADLESS)
    return()
endif()

# The engine's default wind sounds (procedural), cooked at build time.
quench_weather_sounds(cloudsong_weather_sounds)
quench_add_game(cloudsong
    NAME "Cloudsong"
    BUNDLE_ID org.quench.cloudsong
    ICON icon/icon.png
    ASSETS assets
    COOKED_ASSETS cloudsong_weather_sounds
    ORIENTATIONS landscape
    SOURCES src/Game.cpp src/Front.cpp src/Settings.cpp src/Character.cpp src/World.cpp src/Glow.cpp src/Journey.cpp src/Player.cpp
        src/Spirits.cpp src/Sound.cpp)
target_include_directories(cloudsong PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(cloudsong PRIVATE -Wall -Wextra)
endif()
target_link_libraries(cloudsong PRIVATE quench_render quench_physics quench_animation quench_navigation)
quench_add_shaders(cloudsong NAME world SOURCE shaders/world.hlsl
    ENTRIES surface_vs:vertex surface_fs:fragment surface_basic_fs:fragment)
quench_add_shaders(cloudsong NAME foliage SOURCE shaders/foliage.hlsl
    ENTRIES surface_vs:vertex surface_fs:fragment surface_basic_fs:fragment surface_shadow_vs:vertex)
quench_add_shaders(cloudsong NAME glow SOURCE shaders/glow.hlsl ENTRIES glow_vs:vertex glow_fs:fragment)

if(NOT CMAKE_CROSSCOMPILING)
    enable_testing()
    # Scripted offscreen runs; their captures and user files go to the build's captures/, never to the player's.
    set(capture_dir ${CMAKE_CURRENT_BINARY_DIR}/captures)
    foreach(check smoke flight journey sound)
        add_test(NAME ${check} COMMAND cloudsong --window 640x360 --user-dir ${capture_dir}
                 --script ${CMAKE_CURRENT_SOURCE_DIR}/scenarios/${check}.txt)
    endforeach()
    # The touch controls' places are in window points: a 1280x720 window.
    add_test(NAME front COMMAND cloudsong --window 1280x720 --frame-size 640x360 --user-dir ${capture_dir}
             --script ${CMAKE_CURRENT_SOURCE_DIR}/scenarios/front.txt)
    set_tests_properties(smoke flight journey sound front PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL line")

    # Golden images: the key views at fixed frames, compared with tests/golden/metal/ (the default backend on Apple
    # platforms, native Metal, on the M4 Pro Mac) by the engine's image compare tool.
    add_test(NAME captures COMMAND cloudsong --window 480x270 --frame-size 480x270 --user-dir ${capture_dir}
             --script ${CMAKE_CURRENT_SOURCE_DIR}/scenarios/captures.txt)
    set_tests_properties(captures PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL line" FIXTURES_SETUP golden_captures)
    add_executable(cloudsong_image_compare ${quench_SOURCE_DIR}/tools/imagecompare/ImageCompare.cpp)
    target_link_libraries(cloudsong_image_compare PRIVATE SDL3::SDL3-static)
    foreach(view meadow edge isles temple cave title)
        add_test(NAME golden_${view} COMMAND cloudsong_image_compare ${capture_dir}/golden/${view}.png
                 ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/metal/${view}.png --diff ${capture_dir}/golden/${view}_diff.png)
        set_tests_properties(golden_${view} PROPERTIES FIXTURES_REQUIRED golden_captures)
    endforeach()
endif()
