# The engine's script compiler and VM as a library of their own, built from core's sources without the engine's other
# modules, for the host checks that run scripts: tests/script_language, tests/ui_scripts and the UI runtime's checks
# (src/libs/ui: the touch interface's script screens). Each project builds it with its own flags.
include_guard(GLOBAL)
set(storm_script_vm_dir "${CMAKE_CURRENT_LIST_DIR}")
get_filename_component(storm_script_vm_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${storm_script_vm_root}/cmake/glm.cmake")
include("${storm_script_vm_root}/cmake/StormAppContract.cmake")
foreach(storm_script_vm_pin fmt spdlog fast_float sdl)
    storm_fetch_pin(storm_${storm_script_vm_pin} ${storm_script_vm_pin} STORM_POPULATE_ONLY)
    FetchContent_MakeAvailable(storm_${storm_script_vm_pin})
endforeach()
find_library(STORM_SDL_RUNTIME NAMES SDL2-2.0 SDL2 libSDL2-2.0.so.0 REQUIRED)

# storm_script_vm(<target> [OPTIONS <compile flag>...] [LINK_OPTIONS <flag>...]): an OBJECT library; its users get
# its include directories, definitions, flags and links.
function(storm_script_vm target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "OPTIONS;LINK_OPTIONS")
    set(src "${storm_script_vm_root}/src")
    foreach(pin glm fmt spdlog fast_float sdl) # their source directories, wherever this project made them available
        FetchContent_GetProperties(storm_${pin} SOURCE_DIR storm_${pin}_SOURCE_DIR)
    endforeach()
    file(GLOB core_sources CONFIGURE_DEPENDS ${src}/libs/core/src/*.cpp)
    # The engine includes <SDL2/SDL.h>: the pinned headers under that name.
    set(generated "${CMAKE_CURRENT_BINARY_DIR}/${target}-include")
    file(MAKE_DIRECTORY "${generated}")
    file(REMOVE_RECURSE "${generated}/SDL2")
    file(CREATE_LINK "${storm_sdl_SOURCE_DIR}/include" "${generated}/SDL2" SYMBOLIC)
    storm_app_contract_headers()
    add_library(${target} OBJECT ${core_sources}
        ${src}/libs/diagnostics/src/logging.cpp ${src}/libs/diagnostics/src/lifecycle_diagnostics_service.cpp
        ${src}/libs/diagnostics/src/stall_watchdog.cpp ${src}/libs/diagnostics/src/spdlog_sinks/syncable_sink.cpp
        ${src}/libs/window/src/sdl_window.cpp ${src}/libs/window/src/app_events.cpp
        ${storm_fmt_SOURCE_DIR}/src/format.cc ${storm_script_vm_dir}/no_renderer.cpp
        ${storm_script_vm_dir}/watermark.cpp)
    file(GLOB library_includes LIST_DIRECTORIES true ${src}/libs/*/include)
    target_include_directories(${target} PUBLIC ${src}/libs/core/src ${src}/libs/diagnostics/src ${src}/libs/window/src
        ${library_includes} ${src}/libs/util/include ${src}/libs/ui/include ${STORM_APP_CONTRACT_INCLUDE}
        ${src}/libs/math/include ${storm_glm_SOURCE_DIR} ${storm_fmt_SOURCE_DIR}/include
        ${storm_spdlog_SOURCE_DIR}/include ${storm_fast_float_SOURCE_DIR}/include ${generated} ${generated}/SDL2)
    target_compile_definitions(${target} PUBLIC SPDLOG_FMT_EXTERNAL)
    target_compile_features(${target} PUBLIC cxx_std_20)
    # The VM reads chars as signed and type-puns its stack values, as the engine builds it.
    target_compile_options(${target} PUBLIC -fsigned-char -fno-strict-aliasing ${arg_OPTIONS})
    target_link_libraries(${target} PUBLIC "${STORM_SDL_RUNTIME}" -lz -pthread)
    target_link_options(${target} PUBLIC ${arg_LINK_OPTIONS})
endfunction()

# The application's interface scripts as the APK ships them (app/tools/android/mobile_ui_assets.py: the scripts with
# their generated capability header), written to <dir> by the target <target> for the checks that run them.
set(STORM_MOBILE_UI "${storm_script_vm_root}/../app/mobile-ui" CACHE PATH "The application's mobile-ui directory")
function(storm_mobile_ui_layer target dir)
    file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${STORM_MOBILE_UI}/PROGRAM/*")
    set(assets "${STORM_MOBILE_UI}/../tools/android/mobile_ui_assets.py")
    add_custom_command(OUTPUT "${dir}.stamp"
        COMMAND "${Python3_EXECUTABLE}" "${assets}" layer "${STORM_MOBILE_UI}" "${dir}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${dir}.stamp"
        DEPENDS ${sources} "${assets}" "${STORM_MOBILE_UI}/design/build.py" "${STORM_MOBILE_UI}/design/capabilities.json")
    add_custom_target(${target} DEPENDS "${dir}.stamp")
endfunction()
