# The natives of the engine's script libraries (SOURCES) on the production compiler and VM (tests/script_vm), with
# the player's settings of config/. Each case compiled, and loaded from a script cache another compiler wrote.
set(sources ${src}/libs/steam_api/src/steam_api_script_lib.cpp)
add_executable(script_natives_check ${src}/apps/script_check/src/main.cpp ${sources})
target_link_libraries(script_natives_check PRIVATE storm_script_vm nlohmann_json::nlohmann_json)
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
