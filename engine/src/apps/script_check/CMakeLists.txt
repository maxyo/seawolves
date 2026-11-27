# Host tool: compiles a game's whole script tree without starting the game and reports every compile error
# (README.md). Linked as the engine is, so that the script libraries register their natives.
if(ANDROID)
    return()
endif()

STORM_SETUP(
    TARGET_NAME script_check
    TYPE executable
    DEPENDENCIES
    core
    diagnostics
    util
    window
    mimalloc
    ${SDL2_LIBRARIES}
    zlib
    ffi
)
# The script cache (test --cache) is keyed on the binary's build ID.
target_link_options(script_check PRIVATE "-Wl,--build-id=sha1")
target_include_directories(script_check PRIVATE "${CMAKE_SOURCE_DIR}/src/libs/core/src")
storm_build_stamp(script_check)
