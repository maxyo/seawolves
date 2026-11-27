# The script language cases (cases/*.c) on the production compiler and VM: script_check (src/apps/script_check) built
# from core's sources, without the engine's other modules (the cases need only core's built-ins and script_check's own
# test library).
find_library(STORM_SDL_RUNTIME NAMES SDL2-2.0 SDL2 libSDL2-2.0.so.0 REQUIRED)
file(GLOB core_sources CONFIGURE_DEPENDS ${src}/libs/core/src/*.cpp)
set(stamp "${CMAKE_CURRENT_BINARY_DIR}/stamp")
file(WRITE "${stamp}/storm_build_stamp.h" "#define STORM_BUILD_WATERMARK_STRING \"host_checks\"\n")
add_executable(script_language_check ${src}/apps/script_check/src/main.cpp ${core_sources}
    ${src}/libs/diagnostics/src/logging.cpp ${src}/libs/diagnostics/src/lifecycle_diagnostics_service.cpp
    ${src}/libs/diagnostics/src/stall_watchdog.cpp ${src}/libs/diagnostics/src/spdlog_sinks/syncable_sink.cpp
    ${src}/libs/window/src/sdl_window.cpp ${src}/libs/window/src/app_events.cpp
    ${STORM_TEST_SOURCES}/fmt/src/format.cc no_renderer.cpp)
file(GLOB library_includes LIST_DIRECTORIES true ${src}/libs/*/include)
target_include_directories(script_language_check PRIVATE ${src}/libs/core/src ${src}/libs/diagnostics/src
    ${src}/libs/window/src ${library_includes} ${util_include} ${glm_headers} ${STORM_TEST_SOURCES}/fmt/include
    ${STORM_TEST_SOURCES}/spdlog/include ${STORM_TEST_SOURCES}/fast_float/include ${STORM_TEST_SDL_INCLUDE}
    ${STORM_TEST_SDL_INCLUDE}/SDL2 ${stamp})
target_compile_definitions(script_language_check PRIVATE SPDLOG_FMT_EXTERNAL)
# AddressSanitizer: the VM's memory errors fail the cases.
target_compile_options(script_language_check PRIVATE -O1 -g -fsigned-char -fno-strict-aliasing -fsanitize=address)
target_link_libraries(script_language_check PRIVATE "${STORM_SDL_RUNTIME}" -lz -pthread)
# The script cache (--cache) is keyed on the binary's build ID.
target_link_options(script_language_check PRIVATE -fsanitize=address "-Wl,--build-id=sha1")

# Each case compiled, and loaded from a script cache another compiler wrote (README of src/apps/script_check). The
# compiler's logs go below the check's temporary directory.
file(GLOB cases CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cases/*.c)
foreach(check script_language script_language.cache)
    set(mode test)
    if(check STREQUAL script_language.cache)
        list(APPEND mode --cache)
    endif()
    storm_check_test(${check} COMMAND script_language_check ${mode} ${cases}
        ENV ASAN_OPTIONS=detect_leaks=0 XDG_DATA_HOME=${CMAKE_BINARY_DIR}/tmp/${check}
        INPUTS ${src}/libs/core ${src}/apps/script_check)
endforeach()
