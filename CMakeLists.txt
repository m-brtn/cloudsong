cmake_minimum_required(VERSION 3.28)
# macOS 14 is the engine's minimum (its metallibs); the iOS presets set 26.0.
if(CMAKE_HOST_APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 14.0 CACHE STRING "Minimum macOS version" FORCE)
endif()
project(cloudsong VERSION 0.1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The engine at a pinned commit. Working on the engine at the same time: configure with
# -DFETCHCONTENT_SOURCE_DIR_QUENCH=<engine checkout>.
include(FetchContent)
# Quench 0.12.1: stable wide light trails, including jittering attachments and end-on views.
FetchContent_Declare(quench
    GIT_REPOSITORY https://github.com/m-brtn/Quench.git
    GIT_TAG 8170de6868be5fb5a9f72f0d2c067062a9d4c111)
FetchContent_MakeAvailable(quench)

# The host-tools preset builds only the engine's shader compiler and asset cooker (for the iOS builds).
if(QUENCH_HEADLESS)
    return()
endif()

# The engine's default wind sounds (procedural), cooked at build time.
quench_weather_sounds(cloudsong_weather_sounds)
quench_cook_assets(cloudsong_hero SOURCE_DIR art/character-v3/prepared MODELS hero.glb)
quench_cook_assets(cloudsong_hero_motion SOURCE_DIR art/character-v3
    SKELETONS prepared/hero.glb ANIMATIONS animations/hero-motion.glb
    SKELETON prepared/hero.glb RETARGET animations/retarget.json)
quench_cook_assets(cloudsong_world SOURCE_DIR art/world/sanctuary MODELS sanctuary.glb)
quench_cook_assets(cloudsong_windgarden SOURCE_DIR art/world/windgarden MODELS starting_ruins.glb cave_entrance.glb pear_tree.glb harp.glb willow.glb cypress.glb telescope.glb skyship.glb cottage.glb sanctum.glb lantern.glb bird.glb
    silver_birch.glb ancient_oak.glb fan_palm.glb lantern_tree.glb
    fern.glb hydrangea.glb blue_heather.glb cushion_moss.glb berry_bush.glb fossil.glb)
quench_cook_assets(cloudsong_bird SOURCE_DIR art/world/windgarden SKELETONS bird.glb ANIMATIONS bird.glb)
quench_cook_assets(cloudsong_creature SOURCE_DIR art/world/windgarden MODELS meadow_creature.glb
    SKELETONS meadow_creature.glb ANIMATIONS meadow_creature.glb)
quench_add_game(cloudsong
    NAME "Cloudsong"
    BUNDLE_ID org.quench.cloudsong
    ICON icon/cloudsong-daylight.png
    ASSETS assets
    COOKED_ASSETS cloudsong_weather_sounds cloudsong_hero cloudsong_hero_motion cloudsong_world cloudsong_windgarden cloudsong_bird cloudsong_creature
    ORIENTATIONS landscape
    SOURCES src/Game.cpp src/Front.cpp src/Settings.cpp src/Character.cpp src/Hero.cpp src/World.cpp src/Region.cpp src/Places.cpp src/Glow.cpp src/Journey.cpp src/Player.cpp
        src/Spirits.cpp src/MeadowLife.cpp src/Bird.cpp src/MusicKeeper.cpp src/Sound.cpp src/Particles.cpp src/Picture.cpp)
target_include_directories(cloudsong PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(cloudsong PRIVATE -Wall -Wextra)
endif()
target_link_libraries(cloudsong PRIVATE quench_render quench_physics quench_animation quench_navigation
                                        quench_particles)
quench_add_shaders(cloudsong NAME world SOURCE shaders/world.hlsl
    ENTRIES surface_vs:vertex surface_fs:fragment surface_basic_fs:fragment)
quench_add_shaders(cloudsong NAME foliage SOURCE shaders/foliage.hlsl
    ENTRIES surface_vs:vertex surface_fs:fragment surface_basic_fs:fragment surface_shadow_vs:vertex)
quench_add_shaders(cloudsong NAME glow SOURCE shaders/glow.hlsl ENTRIES glow_vs:vertex glow_fs:fragment)
quench_add_shaders(cloudsong NAME picture SOURCE shaders/picture.hlsl ENTRIES picture_vs:vertex picture_fs:fragment)

if(NOT CMAKE_CROSSCOMPILING)
    enable_testing()
    add_executable(cloudsong_settings_tests tests/Settings.cpp src/Settings.cpp)
    target_include_directories(cloudsong_settings_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(cloudsong_settings_tests PRIVATE quench_core)
    add_test(NAME settings COMMAND cloudsong_settings_tests)
    # Scripted offscreen runs; their captures and user files go to the build's captures/, never to the player's.
    set(capture_dir ${CMAKE_CURRENT_BINARY_DIR}/captures)
    foreach(check smoke flight journey sound guide hero region garments windgarden meadow water places picture)
        add_test(NAME ${check} COMMAND cloudsong --window 640x360 --user-dir ${capture_dir}
                 --script ${CMAKE_CURRENT_SOURCE_DIR}/scenarios/${check}.txt)
    endforeach()
    add_test(NAME daylight COMMAND cloudsong --window 1280x720 --frame-size 1280x720 --user-dir ${capture_dir}
             --script ${CMAKE_CURRENT_SOURCE_DIR}/scenarios/daylight.txt)
    set_tests_properties(daylight PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL line")
    # The touch controls' places are in window points: a 1280x720 window.
    add_test(NAME front COMMAND cloudsong --window 1280x720 --frame-size 640x360 --user-dir ${capture_dir}
             --script ${CMAKE_CURRENT_SOURCE_DIR}/scenarios/front.txt)
    set_tests_properties(smoke flight journey sound guide front PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL line")
    set_tests_properties(hero region garments windgarden meadow water places picture PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL line")

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
