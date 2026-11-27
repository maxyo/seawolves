# The engine's host checks: one CMake project, run with CTest (README.md here).
#
#   cmake -S tests -B build/host-ci -G Ninja && cmake --build build/host-ci
#   ctest --test-dir build/host-ci -j8 --output-on-failure        # all (headless: under xvfb-run -a)
#   ctest --test-dir build/host-ci -L renderer                     # one label
#   ctest --test-dir build/host-ci -R '^utf8$'                     # one check
#
# A check that cannot run on this machine exits 77: CTest reports it as skipped (Not Run), never as passed.
# Every check runs with its own TMPDIR, <build>/tmp/<name>, emptied at the start of each ctest run.
cmake_minimum_required(VERSION 3.28)
project(storm_host_checks LANGUAGES C CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/StormBuildCache.cmake")
enable_testing()

# The checks choose their own optimisation: a build type of their own without flags (CMake's would add -O and
# -DNDEBUG, which turns their asserts off; and a dependency's CMake, ncnn's, would set one when there is none).
if(CMAKE_BUILD_TYPE AND NOT CMAKE_BUILD_TYPE STREQUAL "HostCheck")
    message(FATAL_ERROR "The host checks take no CMAKE_BUILD_TYPE (got ${CMAKE_BUILD_TYPE}): reconfigure without it")
endif()
set(CMAKE_BUILD_TYPE HostCheck CACHE STRING "The host checks' build type: no flags of its own" FORCE)
set(CMAKE_C_FLAGS_HOSTCHECK "")
set(CMAKE_CXX_FLAGS_HOSTCHECK "")
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
get_filename_component(STORM_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(STORM_TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(src "${STORM_ROOT}/src")
# The engine's util headers, with the UI's state and command structs the platform headers publish and take and the
# names shared with the application (cmake/StormAppContract.cmake).
include("${STORM_ROOT}/cmake/StormAppContract.cmake")
storm_app_contract_headers()
set(util_include "${src}/libs/util/include" "${src}/libs/ui/include" "${STORM_APP_CONTRACT_INCLUDE}")
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# The pinned engine dependencies (pins.json: fmt, spdlog, fast_float, mimalloc, Catch2, FFmpeg and SDL's headers,
# fetched once per machine by storm_fetch_pin), linked under <build>/sources/<name> (SDL: <build>/sources/SDL) for the
# scripts (--dependency-sources); GLM from cmake/glm.cmake.
set(STORM_TEST_SOURCES "${CMAKE_BINARY_DIR}/sources")
file(MAKE_DIRECTORY "${STORM_TEST_SOURCES}")
foreach(name fmt spdlog fast_float mimalloc catch2 ffmpeg)
    storm_fetch_pin(storm_${name} ${name} STORM_POPULATE_ONLY)
    FetchContent_MakeAvailable(storm_${name})
    file(REMOVE_RECURSE "${STORM_TEST_SOURCES}/${name}")
    file(CREATE_LINK "${storm_${name}_SOURCE_DIR}" "${STORM_TEST_SOURCES}/${name}" SYMBOLIC)
endforeach()
# The audio library's miniaudio, patched as the engine builds it (cmake/engine_dependencies.cmake).
set(miniaudio_patch "${STORM_ROOT}/tools/android/patches/miniaudio-0.11.23-error-cleanup.patch")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${miniaudio_patch}")
storm_fetch_pin(storm_miniaudio miniaudio STORM_POPULATE_ONLY STORM_PATCHES . "${miniaudio_patch}")
FetchContent_MakeAvailable(storm_miniaudio)
storm_fetch_pin(storm_sdl sdl STORM_POPULATE_ONLY)
FetchContent_MakeAvailable(storm_sdl)
set(sdl "${storm_sdl_SOURCE_DIR}")
file(REMOVE_RECURSE "${STORM_TEST_SOURCES}/SDL")
file(CREATE_LINK "${sdl}" "${STORM_TEST_SOURCES}/SDL" SYMBOLIC)
# The checks include <SDL2/SDL.h>: the pinned headers under that name.
set(STORM_TEST_SDL_INCLUDE "${CMAKE_BINARY_DIR}/sdl-include")
file(MAKE_DIRECTORY "${STORM_TEST_SDL_INCLUDE}")
file(REMOVE_RECURSE "${STORM_TEST_SDL_INCLUDE}/SDL2")
file(CREATE_LINK "${sdl}/include" "${STORM_TEST_SDL_INCLUDE}/SDL2" SYMBOLIC)
include("${STORM_ROOT}/cmake/glm.cmake")
set(STORM_GLM_SOURCE "${storm_glm_SOURCE_DIR}" CACHE INTERNAL "The pinned GLM")
include("${STORM_ROOT}/cmake/nlohmann_json.cmake")
# Catch2 with one main for the checks that use it.
add_library(storm_catch2_main OBJECT catch2_main.cpp)
target_include_directories(storm_catch2_main PUBLIC "${STORM_TEST_SOURCES}/catch2/single_include")
find_package(JNI COMPONENTS JVM)

# The runner's environment of every check. Checks that run the compiler themselves compile with $CXX through the
# Storm ccache launcher (tools/ccache/cxx); the scripts find the pinned sources through these variables.
set(STORM_SCRIPT_ENV
    "CXX=${STORM_ROOT}/tools/ccache/cxx" "STORM_CCACHE_CXX=${CMAKE_CXX_COMPILER}"
    "STORM_GLM_SOURCE=${storm_glm_SOURCE_DIR}")
set(STORM_VIRTUALGL "$ENV{STORM_VIRTUALGL}" CACHE PATH "VirtualGL for the checks that need the host GPU (msrtt_reuse)")

include(host_checks.cmake)

# Programs whose exit code is the check.
storm_host_check(android_auto_preset INCLUDES ${src}/libs/renderer/src ${fmt_headers} DEFINES FMT_HEADER_ONLY
    OPTIONS ${warnings} ${asan_strict} CATCH2)
storm_host_check(android_bench INCLUDES ${src}/libs/renderer/src ${src}/libs/renderer_bgfx/include ${fmt_headers}
    DEFINES FMT_HEADER_ONLY OPTIONS ${warnings} ${asan_strict} CATCH2)
storm_host_check(android_camera_look INCLUDES ${glm_headers} ${util_include} OPTIONS ${asan} CATCH2)
storm_host_check(android_camera_zoom INCLUDES ${util_include} OPTIONS ${asan} CATCH2)
storm_host_check(android_combat_input INCLUDES ${util_include} OPTIONS ${asan} CATCH2)
storm_host_check(android_deck_walk INCLUDES ${util_include} ${src}/libs/sea_cameras/src OPTIONS ${asan} CATCH2)
storm_host_check(android_dialogue_input INCLUDES stubs ${util_include} ${src}/libs/dialog/src
    ${src}/libs/ui/include
    DEFINES __ANDROID__ OPTIONS ${warnings} ${asan} CATCH2)
storm_host_check(android_game_message_target INCLUDES ${util_include} OPTIONS ${asan} CATCH2)
storm_host_check(android_gameplay INCLUDES ${util_include} OPTIONS ${asan})
storm_host_check(android_gameplay.snapshot SOURCES snapshot.cpp ${core_fake_sources}
    INCLUDES ${core_fake_android_includes} ${src}/apps/engine/src DEFINES ${core_fake_android_defines}
    OPTIONS -g -Wno-deprecated-declarations ${asan_strict} CATCH2)
storm_host_check(android_held_controls INCLUDES ${util_include} OPTIONS ${asan} CATCH2)
storm_host_check(android_hud_channels INCLUDES ${util_include} OPTIONS ${asan} CATCH2)
storm_host_check(android_hud_invalidate INCLUDES ${util_include} OPTIONS ${asan} CATCH2)
storm_host_check(android_hud_layers INCLUDES ${util_include} OPTIONS ${warnings} ${asan_strict})
storm_host_check(android_interact SOURCES check.cpp ${core_fake_sources}
    INCLUDES ${core_fake_android_includes} ${src}/apps/engine/src DEFINES ${core_fake_android_defines}
    OPTIONS -g -Wno-deprecated-declarations ${asan_strict} CATCH2)
storm_host_check(android_interface_leaving INCLUDES ${src}/libs/xinterface/src OPTIONS ${warnings} ${asan} CATCH2)
storm_host_check(android_movement INCLUDES ${util_include} ${src}/libs/ui/include OPTIONS ${warnings} ${asan_strict}
    CATCH2)
storm_host_check(android_movement.camera SOURCES camera.cpp ${core_fake_sources}
    INCLUDES ${core_fake_android_includes} ${src}/libs/location/src DEFINES ${core_fake_android_defines}
    OPTIONS -O1 -g -Wno-deprecated-declarations ${asan_strict} CATCH2)
storm_host_check(android_movement.motion SOURCES motion.cpp ${core_fake_sources}
    INCLUDES ${core_fake_android_includes} ${src}/libs/location/src DEFINES ${core_fake_android_defines}
    OPTIONS -O1 -g -Wno-deprecated-declarations ${asan_strict} CATCH2)
storm_host_check(android_newgame SOURCES check.cpp ${core_fake_sources} INCLUDES ${core_fake_includes}
    DEFINES ${core_fake_defines} OPTIONS -g -Wno-deprecated-declarations ${asan_strict} CATCH2)
storm_host_check(android_land_activation INCLUDES ${util_include} OPTIONS ${warnings} ${asan_strict})
storm_host_check(android_land_refresh SOURCES check.cpp ${core_fake_sources}
    INCLUDES ${core_fake_includes} ${util_include} ${src}/libs/battle_interface/src/land
    DEFINES ${core_fake_android_defines} OPTIONS -g -Wno-deprecated-declarations ${asan_strict} CATCH2)
storm_host_check(android_land_commands INCLUDES ${util_include} OPTIONS ${asan} CATCH2)
storm_host_check(android_land_commands.missing_source SOURCES missing_source.cpp INCLUDES ${util_include}
    OPTIONS ${asan} CATCH2)
storm_host_check(android_pistol_state INCLUDES ${util_include} OPTIONS ${warnings} ${asan} CATCH2)
storm_host_check(android_render_resolution INCLUDES ${src}/libs/renderer/src OPTIONS ${warnings} ${asan_strict} CATCH2)
storm_host_check(android_runtime INCLUDES ${util_include} ${STORM_TEST_SDL_INCLUDE})
storm_host_check(android_sea_navigator INCLUDES ${util_include} OPTIONS ${asan} CATCH2)
storm_host_check(android_sea_orders INCLUDES ${util_include} OPTIONS ${asan} CATCH2)
storm_host_check(android_sea_quick_action INCLUDES ${util_include} OPTIONS ${asan} CATCH2)
storm_host_check(android_ship_telemetry INCLUDES ${util_include} OPTIONS ${asan} CATCH2)
storm_host_check(android_shortcuts INCLUDES ${util_include} OPTIONS ${asan} CATCH2)
storm_host_check(android_spyglass INCLUDES ${util_include} OPTIONS ${asan} CATCH2)
storm_host_check(android_time_scale INCLUDES ${src}/apps/engine/src OPTIONS -Wall -Werror ${asan} CATCH2)
storm_host_check(android_ui_bootstrap INCLUDES ${src}/libs/core/src ${util_include} ${CMAKE_CURRENT_LIST_DIR}/core_fake
    DEFINES __ANDROID__ OPTIONS ${warnings} ${asan_strict} CATCH2)
storm_host_check(android_window SOURCES check.cpp ${src}/libs/window/src/sdl_window.cpp
    INCLUDES ${STORM_TEST_SDL_INCLUDE} ${src}/libs/window/include ${src}/libs/window/src
    DEFINES __ANDROID__ OPTIONS -pthread ${asan} -fno-omit-frame-pointer LIBS ${sdl_runtime}
    ENV SDL_VIDEODRIVER=dummy CATCH2)
set(worldmap_interface ${src}/libs/battle_interface/src/world_map_interface)
storm_host_check(android_worldmap INCLUDES ${core_fake_android_includes} ${worldmap_interface}
    DEFINES ${core_fake_android_defines} OPTIONS -O1 -g ${asan_strict})
storm_host_check(android_worldmap.control_cache SOURCES control_cache.cpp
    INCLUDES ${src}/libs/pcs_controls/src ${src}/libs/core/include ${src}/libs/shared_headers/include ${util_include}
        ${CMAKE_CURRENT_LIST_DIR}/core_fake
    DEFINES __ANDROID__ OPTIONS -O1 -g ${asan_strict})
storm_host_check(android_worldmap.snapshot SOURCES snapshot.cpp ${core_fake_sources}
    INCLUDES ${core_fake_android_includes} ${src}/apps/engine/src DEFINES ${core_fake_android_defines}
    OPTIONS -O1 -g -Wno-deprecated-declarations ${asan_strict})
storm_host_check(app_contract INCLUDES ${util_include} ${storm_json_SOURCE_DIR}/include OPTIONS ${warnings} ${asan_strict}
    ARGS ${STORM_ROOT}/tools/android/app_contract.json INPUTS ${STORM_ROOT}/tools/android/app_contract.json)
foreach(sanitizer address thread)
    if(sanitizer STREQUAL address)
        set(flags ${asan})
    else()
        set(flags -fsanitize=thread)
    endif()
    storm_host_check(app_events.${sanitizer} SOURCES check.cpp ${src}/libs/window/src/app_events.cpp
        INCLUDES ${STORM_TEST_SDL_INCLUDE} ${src}/libs/window/include
        OPTIONS -pthread -g ${flags} -fno-omit-frame-pointer LIBS ${sdl_runtime}
        ENV SDL_VIDEODRIVER=dummy TSAN_OPTIONS=ignore_noninstrumented_modules=1 TIMEOUT 120 CATCH2)
endforeach()
storm_host_check(atomic_save INCLUDES ${src}/libs/core/src OPTIONS -O2 ${warnings} ${asan_strict} LIBS -lz
    CATCH2)
storm_host_check(background_read SOURCES check.cpp ${src}/libs/core/src/background_read.cpp
    INCLUDES ${glm_headers} ${src}/libs/core/include ${src}/libs/geometry/src ${fmt_headers}
    DEFINES FMT_HEADER_ONLY SPDLOG_FMT_EXTERNAL
    OPTIONS -O1 -g -Wall -Wextra -Wno-multichar ${asan_strict} -pthread CATCH2)
# The CPU admission of the bakes and the relief networks; the second run exits while a bake waits for a permit.
storm_host_check(cpu_admission SOURCES check.cpp ${src}/libs/renderer_bgfx/fx/src/bakes.cpp ${render_log_source}
    INCLUDES ${src}/libs/renderer_bgfx/fx/include ${render_log_headers} DEFINES ${render_log_defines}
    OPTIONS -O0 -pthread -UNDEBUG ${warnings} TIMEOUT 300)
storm_check_test(cpu_admission.exit COMMAND cpu_admission --exit-waiting-bake LABELS host TIMEOUT 300)
storm_host_check(desktop_folders INCLUDES ${util_include} ${storm_json_SOURCE_DIR}/include OPTIONS ${warnings} ${asan_strict}
    CATCH2)
storm_host_check(display_options INCLUDES ${util_include} ${storm_json_SOURCE_DIR}/include
    OPTIONS ${warnings} ${asan_strict} CATCH2)
storm_host_check(flame_flicker INCLUDES ${glm_headers} ${src}/libs/core/include ${src}/libs
    OPTIONS -O1 -g ${warnings} ${asan_strict} -fno-omit-frame-pointer CATCH2)
storm_host_check(frame_pacing INCLUDES ${src}/libs/renderer_bgfx/src OPTIONS ${warnings} ${asan_strict} CATCH2)
storm_host_check(frame_work INCLUDES ${util_include} OPTIONS ${warnings} ${asan_strict} CATCH2)
storm_host_check(frame_timer INCLUDES ${src}/libs/core/src OPTIONS ${warnings} ${asan_strict} CATCH2)
storm_host_check(generation_priority INCLUDES ${glm_headers} ${src}/libs/renderer_bgfx/src/effects
    OPTIONS ${warnings} ${asan_strict} CATCH2)
storm_host_check(grass_pack INCLUDES ${src}/libs/location/src ${STORM_ROOT}/tools/game_checks/grass_pack
    OPTIONS -O2 ${warnings} ${asan_strict} CATCH2)
storm_host_check(haptics INCLUDES ${util_include} OPTIONS ${warnings} ${asan_strict} CATCH2)
storm_host_check(host_hooks INCLUDES ${util_include} OPTIONS ${warnings} ${asan_strict} -pthread)
foreach(program strings desktop android_queue)
    set(sources input.cpp ${src}/libs/input/src/sdl_input.cpp)
    set(defines "")
    set(options -pthread)
    set(libs ${sdl_runtime})
    if(program STREQUAL strings)
        set(sources strings.cpp ${src}/libs/core/src/strings_list.cpp)
        set(options -pthread ${asan} -fno-omit-frame-pointer -g)
        set(libs "")
    elseif(program STREQUAL android_queue)
        set(defines __ANDROID__ STORM_INPUT_HOST_TEST)
    endif()
    storm_host_check(input_std.${program} SOURCES ${sources}
        INCLUDES ${STORM_TEST_SDL_INCLUDE} ${sdl}/include ${src}/libs/input/include ${src}/libs/input/src
                 ${src}/libs/core/src ${util_include} ${src}/libs/renderer_bgfx/include
        DEFINES ${defines} OPTIONS ${options} LIBS ${libs} LABELS display CATCH2)
endforeach()
storm_host_check(island_impact INCLUDES ${src}/libs/touch/src
    OPTIONS -O1 -g ${warnings} ${asan} -fno-omit-frame-pointer CATCH2)
storm_host_check(island_map_zipper SOURCES check.cpp ${src}/libs/island/src/map_zipper.cpp
    INCLUDES stubs ${src}/libs/island/src ${util_include} ${fmt_headers}
    DEFINES FMT_HEADER_ONLY SPDLOG_FMT_EXTERNAL OPTIONS -O1 -g ${warnings} ${asan_strict} -pthread CATCH2)
storm_host_check(island_overhang INCLUDES ${glm_headers} ${src}/libs/island/include
    OPTIONS -O1 -g ${warnings} ${asan} -fno-omit-frame-pointer CATCH2)
storm_host_check(island_zap INCLUDES ${src}/libs/island/include OPTIONS -O1 -g ${warnings} ${asan} CATCH2)
if(JNI_FOUND)
    storm_host_check(jni_text INCLUDES ${util_include} ${JNI_INCLUDE_DIRS}
        OPTIONS ${warnings} ${asan_strict} STD 17 CATCH2)
else()
    storm_check_test(jni_text COMMAND "${Python3_EXECUTABLE}" -c "raise SystemExit('jni_text needs a JDK (jni.h): none found')")
endif()
storm_host_check(scene_reveal SOURCES check.cpp ${render_log_source}
    INCLUDES ${src}/libs/renderer_bgfx/src ${src}/libs/renderer_bgfx/include ${render_log_headers}
    DEFINES ${render_log_defines} OPTIONS ${warnings} ${asan_strict} CATCH2)
storm_host_check(scene_readiness.loading DIRECTORY scene_readiness_check SOURCES loading.cpp ${core_fake_sources}
    INCLUDES ${core_fake_includes} ${STORM_TEST_SDL_INCLUDE}/SDL2 ${src}/libs/renderer/src ${src}/libs/location/src
    DEFINES ${core_fake_defines} OPTIONS -g -Wno-deprecated-declarations ${asan_strict} CATCH2)
storm_host_check(scene_readiness.schedule DIRECTORY scene_readiness_check SOURCES schedule.cpp
    INCLUDES ${src}/libs/renderer_bgfx/include ${util_include} OPTIONS ${warnings} ${asan_strict} CATCH2)
storm_host_check(loading_upload INCLUDES ${src}/libs/renderer_bgfx/src ${src}/libs/renderer_bgfx/include
    OPTIONS ${warnings} ${asan_strict} CATCH2)
storm_host_check(load_watch INCLUDES ${src}/libs/core/include ${src}/libs/xinterface/src
    OPTIONS -g ${asan_strict} CATCH2)
storm_host_check(location_light_choice INCLUDES ${glm_headers} ${src}/libs/location/src
    OPTIONS -O1 -g ${warnings} ${asan} -fno-omit-frame-pointer CATCH2)
storm_host_check(game_profile INCLUDES ${src}/libs/core/include OPTIONS ${warnings} ${asan_strict} CATCH2)
storm_host_check(natives_ext SOURCES check.cpp ${src}/libs/core/src/natives_logic.cpp
    INCLUDES ${src}/libs/core/src ${util_include} ${fmt_headers} DEFINES FMT_HEADER_ONLY OPTIONS ${warnings} ${asan_strict}
    CATCH2)
storm_host_check(log_rotation INCLUDES ${src}/libs/diagnostics/include OPTIONS ${warnings} ${asan_strict} CATCH2)
storm_host_check(logging SOURCES check.cpp ${src}/libs/diagnostics/src/logging.cpp
        ${src}/libs/diagnostics/src/spdlog_sinks/syncable_sink.cpp
    INCLUDES ${src}/libs/diagnostics/include ${src}/libs/diagnostics/src ${util_include} ${STORM_TEST_SDL_INCLUDE}
             ${fmt_headers}
    DEFINES FMT_HEADER_ONLY SPDLOG_FMT_EXTERNAL OPTIONS ${asan_strict} -pthread CATCH2)
storm_host_check(message_diagnostics SOURCES ${src}/libs/core/src/message.cpp check.cpp
    INCLUDES ${glm_headers} ${src}/libs/core/include ${util_include}
             ${STORM_TEST_SOURCES}/fmt/include ${src}/libs/renderer_bgfx/include
    DEFINES FMT_HEADER_ONLY OPTIONS -g ${asan} -fno-omit-frame-pointer -fno-sanitize-recover=all CATCH2)
storm_host_check(mobile_action_frame INCLUDES ${src}/libs/xinterface/src OPTIONS -O1 -g ${asan} CATCH2)
foreach(check alchemy goods_transfer sails_gerald table_game)
    storm_host_check(mobile_${check} INCLUDES ${src}/libs/xinterface/src
        OPTIONS -g -O1 ${asan} -fno-omit-frame-pointer CATCH2)
endforeach()
storm_host_check(mobile_officers SOURCES selection.cpp INCLUDES ${src}/libs/xinterface/src OPTIONS -g ${asan_strict} CATCH2)
foreach(check mobile_owners script_state_generation)
    storm_host_check(${check} SOURCES check.cpp ${src}/libs/core/src/attributes.cpp
        INCLUDES ${CMAKE_CURRENT_LIST_DIR}/mobile_inventory/stubs ${src}/libs/core/include ${util_include}
                 ${src}/libs/xinterface/src
        DEFINES EX_OFF OPTIONS -O1 -g -Wno-deprecated-declarations ${asan} -fno-omit-frame-pointer "SHELL:-include memory" CATCH2)
endforeach()
storm_host_check(mobile_inventory SOURCES check.cpp ${src}/libs/core/src/attributes.cpp
    INCLUDES stubs ${src}/libs/core/include ${util_include} ${src}/libs/xinterface/src
    DEFINES EX_OFF OPTIONS -O1 -g -Wno-deprecated-declarations ${asan_strict} "SHELL:-include memory")
storm_host_check(mobile_inventory.button SOURCES button.cpp
    INCLUDES ${src}/libs/xinterface/src ${src}/libs/shared_headers/include OPTIONS -g ${asan_strict})
storm_host_check(mobile_journal SOURCES check.cpp ${core_fake_sources}
    INCLUDES ${core_fake_includes} ${src}/libs/xinterface/src DEFINES ${core_fake_defines}
    OPTIONS -O1 -g -Wno-deprecated-declarations ${asan_strict} CATCH2)
storm_host_check(mobile_nation_relations SOURCES check.cpp ${core_fake_sources}
    INCLUDES ${core_fake_includes} ${src}/libs/xinterface/src DEFINES ${core_fake_defines}
    OPTIONS -O1 -g -Wno-deprecated-declarations ${asan_strict} CATCH2)
storm_host_check(mobile_picture_tabs SOURCES check.cpp ${src}/libs/xinterface/src/mobile_image_bridge.cpp
        ${src}/libs/xinterface/src/mobile_image_cache.cpp
    INCLUDES ${core_fake_android_includes} ${src}/libs/xinterface/src DEFINES ${core_fake_android_defines}
    OPTIONS -O1 -g ${asan_strict})
storm_host_check(mobile_save_list SOURCES check.cpp ${core_fake_sources} ${src}/libs/core/src/message.cpp
        ${src}/libs/xinterface/src/mobile_save_facts.cpp
    INCLUDES ${core_fake_includes} ${src}/libs/xinterface/src DEFINES ${core_fake_defines}
    OPTIONS -O1 -g -Wno-deprecated-declarations ${asan_strict} CATCH2)
storm_host_check(mobile_settings INCLUDES ${src}/libs/xinterface/src ${util_include} OPTIONS -O1 -g ${asan_strict})
storm_host_check(mobile_shipyard INCLUDES . ${src}/libs/xinterface/src OPTIONS -g -O1 ${asan} -fno-omit-frame-pointer CATCH2)
storm_host_check(mobile_tables SOURCES check.cpp ${core_fake_sources}
    INCLUDES ${core_fake_includes} ${src}/libs/xinterface/src DEFINES ${core_fake_defines}
    OPTIONS -O1 -g -Wno-deprecated-declarations ${asan_strict} CATCH2)
storm_host_check(model_trace INCLUDES ${glm_headers} ${src}/libs/model/src
    OPTIONS -O1 ${warnings} ${asan} CATCH2)
foreach(sanitizer thread address)
    if(sanitizer STREQUAL address)
        set(flags ${asan})
    else()
        set(flags -fsanitize=thread)
    endif()
    storm_host_check(parallel_for.${sanitizer} INCLUDES ${util_include}
        OPTIONS -O1 -g -pthread ${warnings} ${flags} -fno-sanitize-recover=all CATCH2)
endforeach()
storm_host_check(path_fingerprint INCLUDES ${util_include} OPTIONS ${warnings} CATCH2)
storm_host_check(relief_cpu_tiles SOURCES check.cpp ${render_log_source}
    INCLUDES ${src}/libs/relief ${render_log_headers} DEFINES ${render_log_defines} OPTIONS ${warnings} ${asan_strict}
    CATCH2)
storm_host_check(relief_device_choice INCLUDES ${src}/libs/relief OPTIONS ${warnings} ${asan_strict} CATCH2)
storm_host_check(relief_staged SOURCES check.cpp ${src}/libs/renderer_bgfx/src/effects/relief_generator.cpp
    INCLUDES ${glm_headers} ${CMAKE_CURRENT_LIST_DIR}/relief_scheduler/stubs ${src}/libs/renderer_bgfx/src
             ${src}/libs/renderer_bgfx/include ${src}/libs/renderer_bgfx/fx/include ${src}/libs/relief
             ${src}/libs/renderer_bgfx/src/texture/core ${render_log_headers}
    DEFINES ${render_log_defines}
    OPTIONS -O0 -g -pthread ${warnings} -fno-access-control -ffunction-sections -fdata-sections ${asan_strict}
    LIBS -Wl,--gc-sections CATCH2)
storm_host_check(storage INCLUDES ${util_include} OPTIONS ${warnings} ${asan_strict} CATCH2)
storm_host_check(resource_index INCLUDES ${util_include} OPTIONS -O2 ${warnings} CATCH2)
storm_host_check(resource_overlay INCLUDES ${util_include}
    OPTIONS -O1 -g -Wall -Wextra ${asan_strict} TIMEOUT 120 CATCH2)
storm_host_check(sail_limit_point INCLUDES ${glm_headers} OPTIONS -O1 ${warnings} ${asan} CATCH2)
storm_host_check(save_autosave INCLUDES ${src}/libs/xinterface/src OPTIONS -O1 ${warnings} ${asan_strict} CATCH2)
foreach(check load dangling_references loaded_object_handles)
    set(sources ${check}.cpp ${core_fake_sources} ${src}/libs/core/src/s_vartab.cpp)
    if(check STREQUAL loaded_object_handles)
        list(APPEND sources ${src}/libs/core/src/entity_manager.cpp ${src}/libs/core/src/entity_container_cache.cpp
            ${src}/libs/core/src/message.cpp)
    endif()
    storm_host_check(save_state.${check} SOURCES ${sources} INCLUDES ${core_fake_includes} DEFINES ${core_fake_defines}
        OPTIONS -g -Wno-deprecated-declarations ${asan_strict} -fno-omit-frame-pointer)
endforeach()
storm_host_check(save_check INCLUDES ${src}/libs/core/src OPTIONS ${warnings} ${asan_strict} LIBS -lz CATCH2)
# A real save of the game: $STORM_BOOT_SAVE (Not Run without it).
storm_host_check(save_check.real SOURCES real.cpp INCLUDES ${src}/libs/core/src OPTIONS ${warnings} ${asan_strict}
    LIBS -lz)
storm_host_check(save_trash INCLUDES ${src}/libs/core/src OPTIONS ${warnings} ${asan_strict} LIBS -lz CATCH2)
storm_host_check(scene_detail INCLUDES ${src}/libs/renderer/src OPTIONS ${warnings} ${asan_strict} CATCH2)
storm_host_check(script_cache_files INCLUDES ${src}/libs/core/src ${util_include}
    OPTIONS ${warnings} ${asan_strict} CATCH2)
foreach(check call_arguments dangling_aref dynamic_call)
    set(sources ${check}.cpp ${core_fake_sources})
    if(check STREQUAL call_arguments)
        list(APPEND sources ${src}/libs/core/src/s_stack.cpp ${src}/libs/core/src/s_functab.cpp)
    elseif(check STREQUAL dangling_aref)
        list(APPEND sources ${src}/libs/core/src/message.cpp)
    endif()
    storm_host_check(script_compiler.${check} SOURCES ${sources} INCLUDES ${core_fake_includes}
        DEFINES ${core_fake_defines} OPTIONS -g -Wno-deprecated-declarations ${asan_strict} -fno-omit-frame-pointer)
endforeach()
storm_host_check(script_event_handlers SOURCES check.cpp ${src}/libs/core/src/s_eventtab.cpp
    INCLUDES ${src}/libs/core/src ${src}/libs/core/include ${util_include} ${fmt_headers} DEFINES FMT_HEADER_ONLY
    OPTIONS -Wall -Werror ${asan_strict})
storm_host_check(script_patches INCLUDES ${util_include} OPTIONS ${warnings} ${asan_strict} CATCH2)
# The script patch tool compiles (a build error otherwise).
add_library(script_patch_tool_syntax OBJECT "${STORM_ROOT}/tools/script_patches/script_patch_tool.cpp")
target_include_directories(script_patch_tool_syntax PRIVATE ${util_include})
target_compile_options(script_patch_tool_syntax PRIVATE ${warnings})
storm_host_check(script_vm_names SOURCES check.cpp ${src}/libs/core/src/attributes.cpp ${src}/libs/core/src/vm_verify.cpp
    INCLUDES ${src}/libs/core/include ${src}/libs/core/src ${util_include} ${fmt_headers} .
    DEFINES STORM_VM_VERIFY=1 FMT_HEADER_ONLY SPDLOG_FMT_EXTERNAL
    OPTIONS -O1 -g -Wno-deprecated-declarations ${asan} -fno-omit-frame-pointer -fno-sanitize-recover=all CATCH2)
storm_host_check(script_vm_number_text INCLUDES ${src}/libs/core/include ${src}/libs/core/src OPTIONS -O2 CATCH2)
storm_host_check(service_init INCLUDES ${src}/libs/core/include OPTIONS -g -Wall -Werror ${asan_strict})
storm_host_check(sea_cube_schedule INCLUDES ${src}/libs/sea/src OPTIONS -O2 -g ${asan} -fno-omit-frame-pointer CATCH2)
storm_host_check(sea_fire_side INCLUDES ${util_include} OPTIONS -Wall -Werror ${asan_strict} CATCH2)
foreach(check sea_foam_bow_splash ship_track_sampling)
    storm_host_check(${check} OPTIONS -O1 -g ${asan} -fno-omit-frame-pointer STD 17 CATCH2)
endforeach()
storm_host_check(sea_turbidity SOURCES check.cpp ${src}/libs/renderer_bgfx/fx/src/settings.cpp
    INCLUDES ${glm_headers} ${src}/libs/renderer_bgfx/fx/include OPTIONS -O1 ${asan_strict})
storm_host_check(sea_wave_focus INCLUDES ${glm_headers} OPTIONS -O1 -g ${asan} -fno-omit-frame-pointer STD 17 CATCH2)
storm_host_check(sea_wave_sampling INCLUDES ${glm_headers} ${src}/libs/sea/include OPTIONS -O1 -g ${asan} -fno-omit-frame-pointer
    CATCH2)
storm_host_check(ship_script_protocol SOURCES ${src}/libs/core/src/message.cpp check.cpp
    INCLUDES ${glm_headers} ${src}/libs/core/include ${util_include}
             ${src}/libs/shared_headers/include ${src}/libs/ship/src ${STORM_TEST_SOURCES}/fmt/include
             ${src}/libs/renderer_bgfx/include
    DEFINES FMT_HEADER_ONLY OPTIONS -g ${asan} -fno-omit-frame-pointer -fno-sanitize-recover=all CATCH2)
storm_host_check(process_memory INCLUDES ${util_include} OPTIONS ${warnings} ${asan_strict} CATCH2)
storm_host_check(skinning INCLUDES ${src}/libs/model/src OPTIONS -O2 ${warnings} ${asan_strict} CATCH2)
storm_host_check(stall_watchdog SOURCES check.cpp ${src}/libs/diagnostics/src/stall_watchdog.cpp
    INCLUDES ${src}/libs/diagnostics/include ${util_include} ${fmt_headers} DEFINES FMT_HEADER_ONLY
    OPTIONS ${warnings} -g -O1 -fno-omit-frame-pointer -pthread ${asan_strict} LIBS -rdynamic CATCH2)
set(storage_includes ${src}/libs/core/src ${src}/libs/core/include ${util_include}
    ${STORM_TEST_SOURCES}/fmt/include)
set(storage_options -g ${asan} -fno-omit-frame-pointer -fno-sanitize-recover=all)
storm_host_check(std_storage.entity_manager SOURCES entity_manager.cpp ${src}/libs/core/src/entity_manager.cpp
        ${src}/libs/core/src/entity_container_cache.cpp ${src}/libs/core/src/attributes.cpp
    INCLUDES ${storage_includes} DEFINES FMT_HEADER_ONLY OPTIONS ${storage_options} CATCH2)
storm_host_check(std_storage.named_event SOURCES named_event.cpp
    INCLUDES ${glm_headers} ${storage_includes} ${src}/libs/renderer_bgfx/include
    DEFINES FMT_HEADER_ONLY OPTIONS ${storage_options} CATCH2)
storm_host_check(std_storage.postevents SOURCES postevents.cpp ${src}/libs/core/src/message.cpp
    INCLUDES ${glm_headers} ${storage_includes} ${src}/libs/renderer_bgfx/include
    DEFINES FMT_HEADER_ONLY OPTIONS ${storage_options} CATCH2)
storm_host_check(string_compare SOURCES ${src}/libs/util/testsuite/string_compare.cpp
    INCLUDES ${util_include} ${STORM_TEST_SOURCES}/catch2/single_include
    OPTIONS -g ${asan} -fno-omit-frame-pointer -fno-sanitize-recover=all)
# storm/fx/model_digests.hpp and xxHash for the checks that compile the fx core's sources themselves.
include("${src}/libs/relief/model/digests.cmake")
set(model_digests "${CMAKE_BINARY_DIR}/model_digests")
storm_relief_model_digests("${model_digests}")
include("${src}/libs/renderer_bgfx/cmake/xxhash.cmake")
storm_host_check(relief_scheduler SOURCES check.cpp ${src}/libs/renderer_bgfx/src/effects/relief_generator.cpp
        ${src}/libs/renderer_bgfx/fx/src/upscale.cpp
    INCLUDES stubs ${glm_headers} ${src}/libs/renderer_bgfx/src ${src}/libs/renderer_bgfx/include
             ${src}/libs/renderer_bgfx/fx/include ${util_include} ${src}/libs/relief ${render_log_headers} ${model_digests}
    DEFINES ${render_log_defines}
    OPTIONS -pthread ${warnings} -fno-access-control -ffunction-sections -fdata-sections ${asan_strict}
    LIBS storm_xxhash -Wl,--gc-sections ARGS <tmp> TIMEOUT 300)
storm_host_check(texture_cache_codec SOURCES codec.cpp ${src}/libs/renderer_bgfx/fx/src/upscale.cpp
        ${src}/libs/renderer_bgfx/fx/src/relief_store.cpp ${src}/libs/renderer_bgfx/fx/src/normal_map.cpp
        ${render_log_source}
    INCLUDES ${src}/libs/renderer_bgfx/fx/include ${util_include} ${render_log_headers} ${model_digests}
    DEFINES ${render_log_defines}
    OPTIONS -O0 -ffunction-sections -fdata-sections LIBS storm_xxhash -Wl,--gc-sections CATCH2)
storm_host_check(texture_service SOURCES check.cpp ${src}/libs/renderer_bgfx/src/texture/core/service.cpp
        ${render_log_source}
    INCLUDES stubs ${src}/libs/renderer_bgfx/src ${render_log_headers} DEFINES ${render_log_defines}
    OPTIONS -pthread ${warnings} -fno-access-control ${asan_strict} CATCH2)
storm_host_check(thermal_api INCLUDES ${src}/libs/renderer_bgfx/src/effects OPTIONS ${warnings} ${asan_strict})
storm_host_check(thermal_api.sensor SOURCES poll.cpp ${src}/libs/renderer_bgfx/src/effects/thermal.cpp
        ${render_log_source}
    INCLUDES ${src}/libs/renderer_bgfx/src ${render_log_headers} DEFINES ${render_log_defines}
    OPTIONS ${warnings} ${asan_strict} CATCH2)
storm_host_check(ui_host_dialogue INCLUDES ${src}/libs/ui_host/src ${src}/libs/ui/include OPTIONS ${warnings} ${asan} CATCH2)
storm_host_check(user_string_files INCLUDES ${util_include} ${src}/libs/xinterface/src/string_service
    OPTIONS ${warnings} ${asan} CATCH2)
storm_host_check(utf8 INCLUDES ${util_include}
    OPTIONS -O1 -g -fsigned-char -Wall -Wextra ${asan_strict} TIMEOUT 120 CATCH2)

# The pinned fmt: formatting results and float formatting under ASan+UBSan; an invalid format specifier is a
# compile error (the test builds a target that must not compile).
storm_host_check(fmt SOURCES check.cpp ${STORM_TEST_SOURCES}/fmt/src/format.cc
    INCLUDES ${STORM_TEST_SOURCES}/fmt/include OPTIONS -O1 -g ${asan_strict} -fno-omit-frame-pointer
    ENV ASAN_OPTIONS=halt_on_error=1:detect_leaks=0 CATCH2)
add_library(fmt_rejects_bad_format OBJECT EXCLUDE_FROM_ALL fmt/bad_format.cpp)
target_include_directories(fmt_rejects_bad_format PRIVATE ${STORM_TEST_SOURCES}/fmt/include)
storm_check_test(fmt.rejects_bad_format
    COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target fmt_rejects_bad_format)
set_tests_properties(fmt.rejects_bad_format PROPERTIES PASS_REGULAR_EXPRESSION "invalid format specifier")

storm_host_check(animation_cache SOURCES check.cpp ${src}/libs/animation/src/animation_service_imp.cpp
        ${src}/libs/animation/src/animation_info.cpp ${src}/libs/animation/src/animation_imp.cpp
        ${src}/libs/animation/src/action_info.cpp ${src}/libs/animation/src/action_player_imp.cpp
        ${src}/libs/animation/src/animation_timer_imp.cpp ${src}/libs/animation/src/bone.cpp
    INCLUDES ${glm_headers} stubs ${src}/libs/animation/src ${src}/libs/animation/include ${src}/libs/core/include
             ${util_include}
    DEFINES EX_OFF _DEBUG OPTIONS -g "SHELL:-include cstring" "SHELL:-include memory" ${asan} -fno-omit-frame-pointer
    CATCH2)
storm_host_check(mobile_images SOURCES check.cpp ${src}/libs/xinterface/src/mobile_image_cache.cpp
    INCLUDES ${src}/libs/xinterface/src ${src}/libs/ui/include ${util_include}
    OPTIONS -O1 -g -fno-omit-frame-pointer ${asan} CATCH2)

# The scene data parsers as the engine builds them; "[corpus]" reads the installed game in $STORM_GAME_DIR.
add_subdirectory(${src}/libs/scene_data scene_data_library)
storm_host_check(scene_data.parsers OPTIONS ${warnings} LIBS scene_data CATCH2)
storm_check_test(scene_data.corpus COMMAND scene_data.parsers "[corpus]" LABELS game TIMEOUT 600)

# The audio import tool and its library (tools/audio-import), checked with stb_vorbis (the copy in the pinned
# miniaudio); "[corpus]" reads the installed game in $STORM_GAME_DIR.
add_subdirectory(${STORM_ROOT}/tools/audio-import audio_import_tool)
set(stb_vorbis "${storm_miniaudio_SOURCE_DIR}/extras/stb_vorbis.c")
set_source_files_properties("${stb_vorbis}" PROPERTIES COMPILE_OPTIONS -w)
storm_host_check(audio_import.check SOURCES check.cpp "${stb_vorbis}" INCLUDES "${storm_miniaudio_SOURCE_DIR}/extras"
    LIBS audio_import CATCH2)
storm_check_test(audio_import.corpus COMMAND audio_import.check "[corpus]" LABELS game TIMEOUT 1800)

# Checks with a CMake file of their own.
add_subdirectory(far_lod)
add_subdirectory(glm_math)
add_subdirectory(portability)
add_subdirectory(script_language)
# The renderer's checks need bgfx's build (X11 and OpenGL development files, the pinned shaderc): a build that runs
# the others only can leave them out.
option(STORM_CHECK_RENDERER "the renderer's checks (tests/renderer_bgfx)" ON)
if(STORM_CHECK_RENDERER)
    add_subdirectory(renderer_bgfx)
endif()

# Checks that are scripts (see storm_script_check), with the pinned sources they take and the production code they
# read as text (INPUTS: their run-time inputs for tools/ci/affected.py).
set(glm_source --glm-source ${storm_glm_SOURCE_DIR})
set(dependency_sources --dependency-sources ${STORM_TEST_SOURCES})
storm_host_check(android_interface_draw INCLUDES ${util_include} OPTIONS ${warnings} ${asan_strict} -pthread CATCH2)
storm_script_check(ci_check_tmp ci_check_tmp/run.py INPUTS ${STORM_ROOT}/tools/ci/check_tmp.py)
storm_script_check(module_build_id module_build_id/run.py INPUTS ${src}/libs/util)
storm_host_check(animation_decode SOURCES check.cpp ${src}/libs/animation/src/bone.cpp
    INCLUDES ${glm_headers} ${CMAKE_CURRENT_LIST_DIR}/animation_cache/stubs ${src}/libs/animation/src
             ${src}/libs/core/include ${util_include} ${fmt_headers}
    DEFINES FMT_HEADER_ONLY OPTIONS -g ${asan_strict} -fno-omit-frame-pointer CATCH2)
storm_host_check(ball_splash_lifecycle SOURCES check.cpp ${src}/libs/ball_splash/src/t_splash.cpp
    INCLUDES stubs ${src}/libs/ball_splash/src ${glm_headers} ${util_include} ${src}/libs/renderer_bgfx/include
    OPTIONS -O1 -g ${asan_strict} -fno-omit-frame-pointer CATCH2)
storm_host_check(sail_normals.fill SOURCES check.cpp ${src}/libs/rigging/src/sail_grid.cpp
    INCLUDES ${src}/libs/rigging/src ${glm_headers} OPTIONS -O1 ${warnings} ${asan_strict} CATCH2)
storm_host_check(sail_normals.grid SOURCES grid_check.cpp INCLUDES ${glm_headers}
    OPTIONS -O1 ${warnings} ${asan_strict} CATCH2)
storm_host_check(sea_tree_continuity SOURCES check.cpp ${src}/libs/sea/src/sea_tree.cpp
    INCLUDES ${src}/libs/sea/src ${glm_headers} ${fmt_headers} DEFINES FMT_HEADER_ONLY SPDLOG_FMT_EXTERNAL
    OPTIONS -O2 -g ${asan_strict} -fno-omit-frame-pointer CATCH2)
storm_host_check(sea_vertex_budget SOURCES check.cpp ${src}/libs/sea/src/sea_tree.cpp
    INCLUDES ${src}/libs/sea/src ${glm_headers} ${fmt_headers} DEFINES FMT_HEADER_ONLY SPDLOG_FMT_EXTERNAL
    OPTIONS -O2 -g ${asan_strict} -fno-omit-frame-pointer CATCH2)
storm_host_check(worldmap_labels INCLUDES ${src}/libs/worldmap/src ${glm_headers} OPTIONS ${warnings} ${asan_strict} CATCH2)
storm_host_check(sea_ai_relations INCLUDES ${src}/libs/sea_ai/src ${src}/libs/shared_headers/include
    OPTIONS ${warnings} ${asan_strict} CATCH2)
# The audio library against synthetic clips made with the system ffmpeg, also under ASan and UBSan.
storm_script_check(audio audio/run.py --build-dir ${work}/audio --miniaudio ${storm_miniaudio_SOURCE_DIR}
    INPUTS ${STORM_ROOT}/android/audio ${STORM_ROOT}/tools/android/patches)
storm_script_check(audio.sanitize audio/run.py --build-dir ${work}/audio-sanitize --miniaudio ${storm_miniaudio_SOURCE_DIR}
    --sanitize INPUTS ${STORM_ROOT}/android/audio ${STORM_ROOT}/tools/android/patches)
storm_script_check(video video/run.py ${dependency_sources} --build-dir ${work}/video
    LABELS game TIMEOUT 1800 INPUTS ${src}/libs/video ${STORM_ROOT}/tools/ffmpeg)
storm_script_check(cmake_cross_compile cmake_cross_compile/run.py INPUTS ${STORM_ROOT}/cmake/StormSetup.cmake)
storm_script_check(build_cache_patches build_cache_patches/run.py INPUTS ${STORM_ROOT}/cmake/StormBuildCache.cmake)
# The archive and torrent libraries build their pinned dependencies (sources and builds stay in build/<name>).
storm_script_check(archive archive/run.py --build-dir ${work}/archive LABELS native TIMEOUT 1800
    INPUTS ${STORM_ROOT}/android/archive ${STORM_ROOT}/tools/android/pinned_sources.py ${src}/libs/core/src/save_file.hpp
        ${STORM_ROOT}/tools/android/app_contract.json ${STORM_ROOT}/cmake/StormAppContract.cmake)
storm_script_check(torrent torrent/run.py --build-dir ${work}/torrent LABELS native TIMEOUT 1800
    INPUTS ${STORM_ROOT}/android/torrent ${STORM_ROOT}/tools/android/build_torrent.py
    ${STORM_ROOT}/tools/android/pinned_sources.py ${STORM_ROOT}/android/archive/volume_names.hpp)
# The tools of the gate's boot stage and of the scene sweep.
storm_script_check(ci_affected ci_affected/run.py --work ${CMAKE_BINARY_DIR} LABELS tools INPUTS ${STORM_ROOT}/tools/ci/affected.py)
storm_script_check(ci_boot ci_boot/run.py LABELS tools INPUTS ${STORM_ROOT}/tools/ci/boot_stage.py)
storm_script_check(ci_boot_sweep ci_boot_sweep/run.py LABELS tools INPUTS ${STORM_ROOT}/tools/scene_sweep)
storm_script_check(scene_sweep_links scene_sweep_links/run.py LABELS tools INPUTS ${STORM_ROOT}/tools/scene_sweep)
storm_script_check(scene_sweep_pictures scene_sweep_pictures/run.py LABELS tools INPUTS ${STORM_ROOT}/tools/scene_sweep)
# The format of every C/C++ file of the engine (tools/format/cxx_format.py); NOT_RUN without the NDK's clang-format.
storm_script_check(cxx_format cxx_format/run.py LABELS format)

# Every ctest run starts with empty temporary directories, one per check.
get_property(tmp_dirs GLOBAL PROPERTY STORM_TEST_TMP)
string(JOIN "\n" tmp_lines ${tmp_dirs})
file(WRITE "${CMAKE_BINARY_DIR}/tmp-dirs.txt" "${tmp_lines}\n")
add_test(NAME tmp.reset COMMAND "${CMAKE_COMMAND}" -DROOT=${CMAKE_BINARY_DIR}/tmp
    -DLIST=${CMAKE_BINARY_DIR}/tmp-dirs.txt -P "${CMAKE_CURRENT_LIST_DIR}/reset_tmp.cmake")
set_tests_properties(tmp.reset PROPERTIES FIXTURES_SETUP storm_tmp LABELS setup)
