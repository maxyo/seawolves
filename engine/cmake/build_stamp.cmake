# The commit a native build comes from, taken at BUILD time: the target storm_build_stamp_header runs
# cmake/write_build_stamp.cmake on every build, which rewrites <build>/generated/storm_build_stamp.h only when its
# text changes (a build at the same commit recompiles nothing; a new commit or a newly modified tree recompiles only
# what includes it). The header defines
#   STORM_BUILD_COMMIT     "<commit>" or "<commit>+dirty" (a tree with uncommitted changes)
#   STORM_BUILD_WATERMARK_STRING "<STORM_BUILD_PLATFORM>_<commit, 12 digits>" when the build sets STORM_BUILD_PLATFORM:
#                          what the engine logs first in every session ("Running on ...", main.cpp)
# storm_build_stamp(<target>) puts "storm-build-stamp:<STORM_BUILD_COMMIT>" into the target's binary as a kept
# symbol: the application's build_apk.py reads it from libengine.so, libstorm_ui.so and libbgfx.so and refuses a
# library built from another commit than the checkout it packages.
include_guard(GLOBAL)
set(STORM_BUILD_STAMP_DIR "${CMAKE_BINARY_DIR}/generated")
add_custom_target(storm_build_stamp_header ALL
    COMMAND "${CMAKE_COMMAND}" "-DREPOSITORY=${CMAKE_CURRENT_LIST_DIR}/.." "-DPLATFORM=${STORM_BUILD_PLATFORM}"
        "-DOUTPUT=${STORM_BUILD_STAMP_DIR}/storm_build_stamp.h" -P "${CMAKE_CURRENT_LIST_DIR}/write_build_stamp.cmake"
    BYPRODUCTS "${STORM_BUILD_STAMP_DIR}/storm_build_stamp.h"
    COMMENT "Build stamp" VERBATIM)

function(storm_build_stamp target)
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/build_stamp.cpp")
    target_include_directories(${target} PRIVATE "${STORM_BUILD_STAMP_DIR}")
    add_dependencies(${target} storm_build_stamp_header)
endfunction()
