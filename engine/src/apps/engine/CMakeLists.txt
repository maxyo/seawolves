if(ANDROID)
set(SYSTEM_DEPS log android)
else()
set(SYSTEM_DEPS "ffi")
endif()

STORM_SETUP(
    TARGET_NAME engine
    TYPE executable
    DEPENDENCIES

    # internal
    core
    diagnostics
    util
    window

    # external
    $<$<NOT:$<BOOL:${ANDROID}>>:mimalloc>
    ${SDL2_LIBRARIES}
    zlib

    # system
    ${SYSTEM_DEPS}
)
# The script cache is keyed on the binary's build ID (core compiler.cpp EngineBuildId): always link one.
target_link_options(engine PRIVATE "-Wl,--build-id=sha1")
storm_build_stamp(engine WATERMARK)

if(ANDROID)
    set_target_properties(engine PROPERTIES VERSION "" SOVERSION "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/game")
    # The APK takes the relief's ncnn graphs (assets) from the same <build>/game directory: building the engine puts
    # them there (src/libs/relief/README.md); ncnn itself is linked into libengine.so.
    add_dependencies(engine storm_relief_model_files)
endif()


if(NOT ANDROID)
    # The engine's own settings file (platform/desktop_settings.hpp).
    include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/nlohmann_json.cmake")
    target_link_libraries(engine PRIVATE nlohmann_json::nlohmann_json)

    # The layers laid over the game, installed beside the engine as Android installs them from the APK
    # (README.md, "Where files live"): the engine's techniques, and the application's script fixes for desktop,
    # written by the application's own packager.
    set(STORM_SCRIPT_PATCHES "${CMAKE_SOURCE_DIR}/../app/script-patches" CACHE PATH
        "The application's script-patches folder (sets/<set>/set.json), beside its tools/android/script_patch_assets.py")
    set(packager "${STORM_SCRIPT_PATCHES}/../tools/android/script_patch_assets.py")
    if(NOT EXISTS "${STORM_SCRIPT_PATCHES}/sets" OR NOT EXISTS "${packager}")
        message(FATAL_ERROR "STORM_SCRIPT_PATCHES=${STORM_SCRIPT_PATCHES}: no sets/ or no ${packager}; set it to the "
                            "repository's app/script-patches")
    endif()
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    file(GLOB_RECURSE techniques CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/techniques/*")
    file(GLOB_RECURSE patch_sets CONFIGURE_DEPENDS "${STORM_SCRIPT_PATCHES}/sets/*")
    set(layers "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/layers")
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/layers.stamp"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${layers}/engine-shaders"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/src/techniques"
                "${layers}/engine-shaders/RESOURCE/techniques"
        COMMAND "${Python3_EXECUTABLE}" "${packager}" layer --platform desktop --source "${STORM_SCRIPT_PATCHES}"
                --out "${layers}/script-patches"
        COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_CURRENT_BINARY_DIR}/layers.stamp"
        DEPENDS ${techniques} ${patch_sets} "${packager}"
        COMMENT "Installing the engine's layers in ${layers}"
        VERBATIM)
    add_custom_target(storm_engine_layers DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/layers.stamp")
    add_dependencies(engine storm_engine_layers)
endif()
