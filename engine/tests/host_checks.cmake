# The functions and flags of the host check project (CMakeLists.txt here, renderer_bgfx/CMakeLists.txt).
include_guard(GLOBAL)

# storm_check_test(<name> COMMAND <command...> [ENV <VAR=value>...] [TIMEOUT <s>] [LABELS <label>...]
#                  [INPUTS <path>...] [RUN_SERIAL] [PROCESSORS <n>])
# A test with the properties every check has: skip code 77, a deadlock guard (TIMEOUT, default 300 s), its own
# TMPDIR (<tmp> in COMMAND is that directory) and the label host. INPUTS: files and directories the check reads at
# run time that no build compiles (tools/ci/affected.py selects the check when one of them changes).
function(storm_check_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "RUN_SERIAL" "TIMEOUT;PROCESSORS" "COMMAND;ENV;LABELS;INPUTS")
    set(tmp "${CMAKE_BINARY_DIR}/tmp/${name}")
    list(TRANSFORM arg_COMMAND REPLACE "<tmp>" "${tmp}")
    set(environment "TMPDIR=${tmp}" ${arg_ENV})
    add_test(NAME ${name} COMMAND ${arg_COMMAND})
    set_property(GLOBAL APPEND PROPERTY STORM_TEST_TMP "${tmp}")
    if(NOT arg_TIMEOUT)
        set(arg_TIMEOUT 300)
    endif()
    set_tests_properties(${name} PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT ${arg_TIMEOUT}
        FIXTURES_REQUIRED storm_tmp
        ENVIRONMENT "${environment}"
        LABELS "host;${arg_LABELS}")
    if(arg_INPUTS)
        set_tests_properties(${name} PROPERTIES STORM_INPUTS "${arg_INPUTS}")
    endif()
    if(arg_RUN_SERIAL)
        set_tests_properties(${name} PROPERTIES RUN_SERIAL ON)
    endif()
    if(arg_PROCESSORS)
        set_tests_properties(${name} PROPERTIES PROCESSORS ${arg_PROCESSORS})
    endif()
endfunction()

# storm_host_check(<name> [DIRECTORY <dir>] [SOURCES <file>...] [INCLUDES <dir>...] [DEFINES <def>...]
#                  [OPTIONS <flag>...] [LIBS <lib>...] [STD <n>] [ARGS <arg>...] [CATCH2] + storm_check_test options)
# One check program and its test. Its directory is tests/<dir> (default: the name up to its first '.') when declared
# in tests/CMakeLists.txt, else the declaring directory; relative SOURCES (default check.cpp) and INCLUDES are taken
# there. OPTIONS are compile flags; the sanitizer and thread flags among them also link. A link-only flag such as
# -rdynamic goes into LIBS: Clang refuses it on a compile. CATCH2: the check is written with Catch2 and links the
# shared main.
function(storm_host_check name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "CATCH2;RUN_SERIAL" "DIRECTORY;STD;TIMEOUT;PROCESSORS"
        "SOURCES;INCLUDES;DEFINES;OPTIONS;LIBS;ARGS;ENV;LABELS;INPUTS")
    # A check declared in tests/CMakeLists.txt lives in tests/<dir>; one declared in tests/<dir>/CMakeLists.txt there.
    set(dir "${CMAKE_CURRENT_SOURCE_DIR}")
    if(dir STREQUAL STORM_TESTS_DIR)
        if(NOT arg_DIRECTORY)
            string(REGEX REPLACE "\\..*$" "" arg_DIRECTORY "${name}")
        endif()
        set(dir "${dir}/${arg_DIRECTORY}")
    endif()
    if(NOT arg_SOURCES)
        set(arg_SOURCES check.cpp)
    endif()
    list(TRANSFORM arg_SOURCES PREPEND "${dir}/" REGEX "^[^/$]")
    list(TRANSFORM arg_INCLUDES PREPEND "${dir}/" REGEX "^[^/$]")
    add_executable(${name} ${arg_SOURCES})
    target_include_directories(${name} PRIVATE ${arg_INCLUDES})
    target_compile_definitions(${name} PRIVATE ${arg_DEFINES})
    target_compile_options(${name} PRIVATE ${arg_OPTIONS})
    set(link_options ${arg_OPTIONS})
    list(FILTER link_options INCLUDE REGEX "^-(fsanitize|fno-sanitize|pthread$)")
    target_link_options(${name} PRIVATE ${link_options})
    target_link_libraries(${name} PRIVATE ${arg_LIBS})
    if(arg_STD)
        set_target_properties(${name} PROPERTIES CXX_STANDARD ${arg_STD})
    endif()
    if(arg_CATCH2)
        target_link_libraries(${name} PRIVATE storm_catch2_main)
    endif()
    set(extra "")
    foreach(option RUN_SERIAL)
        if(arg_${option})
            list(APPEND extra ${option})
        endif()
    endforeach()
    foreach(option TIMEOUT PROCESSORS)
        if(arg_${option})
            list(APPEND extra ${option} ${arg_${option}})
        endif()
    endforeach()
    storm_check_test(${name} COMMAND ${name} ${arg_ARGS} ENV ${arg_ENV} LABELS ${arg_LABELS} INPUTS ${arg_INPUTS}
        ${extra})
endfunction()

# storm_script_check(<name> <script> [<arg>...] + storm_check_test options)
# A check that is a Python script doing more than compile and run (it reads production sources, builds pinned
# libraries, drives a tool): run with the scripts' environment. Its own directory counts as its input.
function(storm_script_check name script)
    cmake_parse_arguments(PARSE_ARGV 2 arg "RUN_SERIAL" "TIMEOUT;PROCESSORS" "ENV;LABELS;INPUTS")
    set(extra "")
    if(arg_RUN_SERIAL)
        list(APPEND extra RUN_SERIAL)
    endif()
    foreach(option TIMEOUT PROCESSORS)
        if(arg_${option})
            list(APPEND extra ${option} ${arg_${option}})
        endif()
    endforeach()
    if(NOT arg_TIMEOUT)
        list(APPEND extra TIMEOUT 600) # a compile of sanitized sources on a loaded machine is slow
    endif()
    storm_check_test(${name} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/${script}"
            ${arg_UNPARSED_ARGUMENTS}
        ENV ${STORM_SCRIPT_ENV} ${arg_ENV} LABELS script ${arg_LABELS} INPUTS ${arg_INPUTS} ${extra})
endfunction()

set(warnings -Wall -Wextra -Werror)
set(asan -fsanitize=address,undefined)
set(asan_strict ${asan} -fno-sanitize-recover=all)
set(fmt_headers ${STORM_TEST_SOURCES}/fmt/include ${STORM_TEST_SOURCES}/spdlog/include)
# The renderer's log (renderer_bgfx/log): its header over the pinned spdlog and fmt, header-only, and its source.
set(render_log_headers ${STORM_ROOT}/src/libs/renderer_bgfx/log/include ${fmt_headers})
set(render_log_defines FMT_HEADER_ONLY SPDLOG_FMT_EXTERNAL)
set(render_log_source ${STORM_ROOT}/src/libs/renderer_bgfx/log/log.cpp)
# GLM as the engine includes it: through storm_glm.h, which holds its configuration (src/libs/math/README.md).
set(glm_headers ${STORM_ROOT}/src/libs/math/include ${STORM_GLM_SOURCE})
set(STORM_GLM_HEADERS "${glm_headers}" CACHE INTERNAL "The include directories of GLM (tools/ci/affected.py)")
set(sdl_runtime -Wl,-l:libSDL2-2.0.so.0)
set(work "${CMAKE_BINARY_DIR}/build")
# Engine code that reads the game through `core` (tests/core_fake): the real ATTRIBUTES and DATA with every library's
# headers; for code built as for Android, core_fake also holds the NDK's log and properties and JNI comes from the JDK.
set(core_fake_sources ${STORM_ROOT}/src/libs/core/src/attributes.cpp ${STORM_ROOT}/src/libs/core/src/data.cpp)
file(GLOB library_includes LIST_DIRECTORIES true ${STORM_ROOT}/src/libs/*/include)
set(core_fake_includes ${STORM_TESTS_DIR}/core_fake ${library_includes} ${STORM_ROOT}/src/libs/core/src ${glm_headers}
    ${fmt_headers} ${STORM_TEST_SOURCES}/fast_float/include ${STORM_TEST_SDL_INCLUDE} ${STORM_APP_CONTRACT_INCLUDE})
set(core_fake_defines FMT_HEADER_ONLY SPDLOG_FMT_EXTERNAL)
set(core_fake_android_includes ${core_fake_includes} ${JNI_INCLUDE_DIRS} ${storm_json_SOURCE_DIR}/include)
set(core_fake_android_defines __ANDROID__ SDL_MAIN_HANDLED ${core_fake_defines}) # SDL renames main() on Android
