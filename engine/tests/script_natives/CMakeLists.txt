# The natives of the engine's script libraries (SOURCES) on the production compiler and VM (tests/script_vm), with
# the player's settings of config/. Each case compiled, and loaded from a script cache another compiler wrote.
set(sources ${src}/libs/steam_api/src/steam_api_script_lib.cpp ${src}/libs/renderer/src/graphics_options.cpp
    ${src}/libs/renderer/src/graphics_option_values.cpp)
add_executable(script_natives_check ${src}/apps/script_check/src/main.cpp ${sources})
target_link_libraries(script_natives_check PRIVATE storm_script_vm nlohmann_json::nlohmann_json)
target_include_directories(script_natives_check PRIVATE ${src}/libs/renderer_bgfx/include
    ${src}/libs/renderer_bgfx/fx/include ${src}/libs/scene_environment/include ${glm_headers})
file(GLOB cases CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cases/*.c)
foreach(check script_natives script_natives.cache)
    set(mode test)
    if(check STREQUAL script_natives.cache)
        list(APPEND mode --cache)
    endif()
    storm_check_test(${check} COMMAND script_natives_check ${mode} ${cases}
        ENV ASAN_OPTIONS=detect_leaks=0 XDG_DATA_HOME=${CMAKE_BINARY_DIR}/tmp/${check}
            XDG_CONFIG_HOME=${CMAKE_CURRENT_SOURCE_DIR}/config
        INPUTS ${src}/libs/core ${src}/apps/script_check ${src}/libs/steam_api ${CMAKE_CURRENT_SOURCE_DIR}/config)
endforeach()

# The graphics options (graphics/): a fresh game, its next start (the same settings folder) and another game.
set(graphics ${CMAKE_CURRENT_SOURCE_DIR}/graphics)
set(game_a ${CMAKE_BINARY_DIR}/tmp/script_natives.graphics/game_a)
set(inputs INPUTS ${src}/libs/core ${src}/apps/script_check ${src}/libs/renderer/src ${graphics})
storm_check_test(script_natives.graphics COMMAND script_natives_check test --settings ${game_a} ${graphics}/options.c
    ENV ASAN_OPTIONS=detect_leaks=0 ${inputs})
storm_check_test(script_natives.graphics_kept COMMAND script_natives_check test --settings ${game_a} ${graphics}/kept.c
    ENV ASAN_OPTIONS=detect_leaks=0 ${inputs})
storm_check_test(script_natives.graphics_other_game COMMAND script_natives_check test
    --settings ${CMAKE_BINARY_DIR}/tmp/script_natives.graphics/game_b ${graphics}/other_game.c
    ENV ASAN_OPTIONS=detect_leaks=0 ${inputs})
storm_check_test(script_natives.graphics_maps_shipped COMMAND script_natives_check test ${graphics}/maps_shipped.c
    ENV ASAN_OPTIONS=detect_leaks=0 ${inputs})
set_tests_properties(script_natives.graphics PROPERTIES FIXTURES_SETUP graphics_options)
set_tests_properties(script_natives.graphics_kept script_natives.graphics_other_game PROPERTIES
    FIXTURES_REQUIRED "storm_tmp;graphics_options")
