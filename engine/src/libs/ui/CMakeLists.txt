# Portable game UI runtime: RmlUi documents drawn through the bgfx render backend (inside the
# engine frame for the game, on a bgfx context of its own elsewhere). Stand-alone project for the
# host checks and captures; on Android it is part of the engine build, which makes the APK's
# libstorm_ui.so next to libengine.so and libbgfx.so (one bgfx per process).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.21)
    project(storm-ui LANGUAGES C CXX)
    if(ANDROID)
        message(FATAL_ERROR "The Android UI runtime is built by the engine build (tools/android/ci_engine.sh), with the libbgfx.so it shares with libengine.so")
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/StormBuildCache.cmake")
    set(STORM_ENABLE_UI ON)
endif()
option(STORM_ENABLE_UI "Build the RmlUi based portable game UI runtime" OFF)
if(NOT STORM_ENABLE_UI)
    return()
endif()
# OFF (stand-alone host builds only): the runtime and the checks that draw nothing (storm_ui_pack_check, the
# screen/HUD unit checks), without the bgfx backend, the .tx decoder, captures, the shader build (no host shaderc)
# and bgfx's X11/OpenGL configure. The application's tests/ui_pack_contract builds this way.
option(STORM_UI_RENDER_BACKEND "Build the bgfx render backend, captures and the checks that draw" ON)
if(NOT STORM_UI_RENDER_BACKEND AND NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR "STORM_UI_RENDER_BACKEND=OFF is for the stand-alone host build of src/libs/ui only")
endif()
# Inside the engine build: neither this library nor its third-party sources take the engine's
# global flags (-std=c++20 for C sources, the Windows type headers ...).
set_directory_properties(PROPERTIES INCLUDE_DIRECTORIES "" COMPILE_DEFINITIONS "" COMPILE_OPTIONS "")

include(FetchContent)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Pinned release archives (engine/pins.json).
storm_fetch_pin(storm_ui_freetype freetype)
# bimg's sources only: the runtime decodes the pack's PNG and JPEG pictures with the stb_image bimg bundles (the engine
# build has populated it already; a stand-alone build fetches it here).
storm_fetch_pin(storm_bimg bimg STORM_POPULATE_ONLY)
# Local fixes on top of the RmlUi pin: patches/rmlui-*.patch, applied in the build directory's own patched view of
# the shared source copy (storm_patched_view): a changed patch recompiles only what the changed files reach.
file(GLOB storm_ui_rmlui_patches CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/patches/rmlui-*.patch")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${storm_ui_rmlui_patches})
storm_fetch_pin(storm_ui_rmlui rmlui
    STORM_PATCHES . ${storm_ui_rmlui_patches})

# FreeType without optional codecs: the bundled fonts are plain TrueType outlines.
foreach(codec ZLIB BZIP2 PNG HARFBUZZ BROTLI)
    set(FT_DISABLE_${codec} TRUE CACHE BOOL "" FORCE)
endforeach()
FetchContent_MakeAvailable(storm_ui_freetype storm_bimg)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/nlohmann_json.cmake")
if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_FONT_ENGINE freetype CACHE STRING "" FORCE)
set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(storm_ui_rmlui)
# Builds with debug info (the engine's Android builds: -g) keep only line tables for the UI
# runtime and its third-party code: crash stacks still resolve to function, file and line, and
# libstorm_ui.so ships unstripped in debug APKs without full variable/type info (about 4x larger).
string(TOUPPER "${CMAKE_BUILD_TYPE}" storm_ui_config)
if(" ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${storm_ui_config}} " MATCHES " -g ")
    set(storm_ui_line_tables_only ON)
endif()
function(storm_ui_line_tables)
    if(storm_ui_line_tables_only)
        foreach(target IN LISTS ARGN)
            if(TARGET ${target})
                target_compile_options(${target} PRIVATE -gline-tables-only)
            endif()
        endforeach()
    endif()
endfunction()
storm_ui_line_tables(rmlui_core freetype)
# Licence texts of the pinned dependencies (<build>/licenses), packaged with libstorm_ui.so.
foreach(notice
        "${storm_ui_rmlui_SOURCE_DIR}/LICENSE.txt|RmlUi-MIT.txt"
        "${storm_ui_freetype_SOURCE_DIR}/docs/FTL.TXT|FreeType-FTL.txt"
        "${storm_json_SOURCE_DIR}/LICENSE.MIT|nlohmann-json-MIT.txt")
    string(REPLACE "|" ";" notice "${notice}")
    list(GET notice 0 from)
    list(GET notice 1 to)
    configure_file("${from}" "${CMAKE_BINARY_DIR}/licenses/${to}" COPYONLY)
endforeach()

# bgfx is pinned only by src/libs/renderer_bgfx. A stand-alone build pulls that directory in: it
# pins and configures bgfx (limits, GLES 3.0) and provides the shader build (the cached host
# shaderc). Inside the engine it is there already.
set(storm_ui_renderer_bgfx "${CMAKE_CURRENT_SOURCE_DIR}/../renderer_bgfx")
if(STORM_UI_RENDER_BACKEND AND NOT COMMAND storm_bgfx_embed_shaders)
    add_subdirectory("${storm_ui_renderer_bgfx}" "${CMAKE_CURRENT_BINARY_DIR}/renderer_bgfx" EXCLUDE_FROM_ALL)
endif()

add_library(storm_ui STATIC
    src/ui_host.cpp
    src/host_touch.cpp
    src/host_toasts.cpp
    src/host_hud.cpp
    src/host_taps.cpp
    src/host_reports.cpp
    src/host_screens.cpp
    src/interface_model.cpp
    src/screens/router.cpp
    src/screens/deal.cpp
    src/screens/format.cpp
    src/screens/pictures.cpp
    src/screens/amount.cpp
    src/screens/parse_amount.cpp
    src/screens/registry.cpp
    src/screens/dialogue.cpp
    src/screens/app_settings.cpp
    src/screens/menu_states.cpp
    src/screens/script_screen.cpp
    src/screens/script_props.cpp
    src/component_spec.cpp
    src/screens/launcher.cpp
    src/hud/abilities.cpp
    src/hud/hud.cpp
    src/hud/states.cpp
    src/hud/sea.cpp
    src/hud/script_scene.cpp
    src/hud/script_states.cpp
    src/hud/spyglass.cpp
    src/template.cpp
    src/text_utf8.cpp
    src/components.cpp
    src/behaviors.cpp
    src/touch_motion.cpp
    src/action_trace.cpp
    src/draw_elements.cpp
    src/shadow_element.cpp
    src/icons.cpp
    src/prewarm.cpp
    src/game_image.cpp
    src/picture_atlas.cpp
    src/frame_renderer.cpp)
target_compile_features(storm_ui PUBLIC cxx_std_20)
# The finger physics shared with the game cameras (header-only, standard library only).
target_include_directories(storm_ui PUBLIC include PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}/../util/include")
# The names the UI shares with the Android activity (app_ui_names.hpp, engine cmake/StormAppContract.cmake).
include("${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/StormAppContract.cmake")
storm_app_contract_headers()
target_include_directories(storm_ui PRIVATE "${STORM_APP_CONTRACT_INCLUDE}")
target_link_libraries(storm_ui PUBLIC RmlUi::Core nlohmann_json::nlohmann_json)
target_include_directories(storm_ui SYSTEM PRIVATE "${storm_bimg_SOURCE_DIR}/3rdparty") # stb/stb_image.h
if(ANDROID)
    target_sources(storm_ui PRIVATE src/asset_files.cpp) # the pack in the APK assets
    target_link_libraries(storm_ui PRIVATE android)
endif()

# The render backend: bgfx. Only the frame owner's interface header is used (no symbols), so the
# stand-alone build does not need the engine's renderer library.
if(STORM_UI_RENDER_BACKEND)
    file(GLOB storm_ui_shader_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.sc")
    list(FILTER storm_ui_shader_sources EXCLUDE REGEX "varying\\.def\\.sc$")
    storm_bgfx_embed_shaders(TARGET storm_ui_shaders NAMESPACE storm_ui_shaders
        VARYING "${CMAKE_CURRENT_SOURCE_DIR}/shaders/varying.def.sc" SOURCES ${storm_ui_shader_sources})
    add_library(storm_ui_bgfx STATIC src/backend_bgfx.cpp)
    target_include_directories(storm_ui_bgfx PUBLIC "${storm_ui_renderer_bgfx}/include")
    target_link_libraries(storm_ui_bgfx PUBLIC storm_ui storm_bgfx_runtime PRIVATE storm_ui_shaders)
endif()

if(ANDROID)
    # libstorm_ui.so, the one UI runtime of the APK: the game (src/libs/ui_host in libengine.so)
    # and the application screens without the engine (launcher, downloads, gallery: their Java
    # view owns the EGL context, android/jni_host.cpp) both load it. It takes bgfx from
    # libbgfx.so (storm_bgfx_runtime), so the game process has one bgfx. Built by the engine's
    # Android build, which packages it with libengine.so and libbgfx.so.
    add_library(storm_ui_runtime SHARED android/jni_host.cpp)
    target_link_libraries(storm_ui_runtime PRIVATE -Wl,--whole-archive storm_ui storm_ui_bgfx -Wl,--no-whole-archive android log EGL
        PUBLIC storm_bgfx_runtime)
    target_link_options(storm_ui_runtime PRIVATE -Wl,--no-undefined -Wl,-z,max-page-size=16384)
    target_include_directories(storm_ui_runtime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../util/include") # platform/jni_text.hpp
    set_target_properties(storm_ui_runtime PROPERTIES OUTPUT_NAME storm_ui LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/game")
    # Users compile against the runtime's headers (and RmlUi's, nlohmann::json's) but link only it.
    target_include_directories(storm_ui_runtime INTERFACE "$<TARGET_PROPERTY:storm_ui_bgfx,INTERFACE_INCLUDE_DIRECTORIES>")
    target_compile_definitions(storm_ui_runtime INTERFACE "$<TARGET_PROPERTY:storm_ui_bgfx,INTERFACE_COMPILE_DEFINITIONS>")
    target_compile_features(storm_ui_runtime INTERFACE cxx_std_20)
    include("${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/build_stamp.cmake")
    storm_build_stamp(storm_ui_runtime)
    storm_ui_line_tables(storm_ui storm_ui_bgfx storm_ui_runtime)
elseif(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # The stand-alone host build: its tools and checks (the desktop engine's touch build links only the runtime).
    # lunasvg (with plutovg) only here: the icon atlas is rasterised on the developer's machine (tools/icon_bake.cpp,
    # run by app/mobile-ui/design/bake_icons.py), and storm_ui_shot writes its PNGs with plutovg.
    storm_fetch_pin(storm_ui_lunasvg lunasvg)
    set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(PLUTOVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LUNASVG_DISABLE_LOAD_SYSTEM_FONTS ON CACHE BOOL "" FORCE)
    set(PLUTOVG_DISABLE_FONT_FACE_CACHE_LOAD ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(storm_ui_lunasvg)
    add_executable(storm_ui_icon_bake tools/icon_bake.cpp)
    target_include_directories(storm_ui_icon_bake PRIVATE "${storm_ui_lunasvg_SOURCE_DIR}/plutovg/source")
    target_link_libraries(storm_ui_icon_bake PRIVATE lunasvg::lunasvg nlohmann_json::nlohmann_json)
    if(STORM_UI_RENDER_BACKEND)
        # Headless captures for design checks: bgfx GLES 3.0 on a surfaceless EGL display, PNG output. The game's
        # pictures are read from .tx files by the renderer's texture core (the game takes them from the renderer).
        add_executable(storm_ui_shot tools/ui_shot.cpp)
        target_link_libraries(storm_ui_shot PRIVATE storm_ui_bgfx storm_texture_core plutovg::plutovg storm_ui_script_game
                              ${CMAKE_DL_LIBS})
    endif()
    # The pack contract check (tools/pack_check.cpp): a real pack with the real adapters on the
    # recorded fixtures. The application's host checks run it on its pack (tests/ui_pack_contract);
    # here it runs when STORM_UI_PACK names a built pack.
    # The touch interface's scripts on the engine's script VM over the fixture game (tools/script_game.h): the checks
    # that draw the pack's screens draw the script screens too.
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    include("${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/script_vm/script_vm.cmake")
    storm_script_vm(storm_ui_script_vm)
    storm_mobile_ui_layer(storm_ui_scripts_layer "${CMAKE_BINARY_DIR}/mobile-ui-layer")
    add_library(storm_ui_script_game STATIC tools/script_game.cpp ../ui_host/src/scripts/script_segments.cpp
        ../ui_host/src/scripts/script_port.cpp)
    add_dependencies(storm_ui_script_game storm_ui_scripts_layer)
    target_include_directories(storm_ui_script_game PUBLIC ../ui_host/src/scripts)
    target_compile_definitions(storm_ui_script_game PRIVATE
        STORM_UI_SCRIPT_GAME="${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/ui_scripts/game"
        STORM_UI_SCRIPT_LAYER="${CMAKE_BINARY_DIR}/mobile-ui-layer"
        STORM_UI_SCRIPT_LIST="${STORM_MOBILE_UI}/design/scripts.json")
    target_link_libraries(storm_ui_script_game PUBLIC storm_ui storm_ui_script_vm)
    add_executable(storm_ui_pack_check tools/pack_check.cpp)
    target_link_libraries(storm_ui_pack_check PRIVATE storm_ui storm_ui_script_game)
    set(STORM_UI_PACK "" CACHE PATH "A built UI pack (the application's build.py --pack output) for the ui_pack_contract test")
    enable_testing()
    set(models "${CMAKE_CURRENT_SOURCE_DIR}/tests/models")
    set(states "${CMAKE_CURRENT_SOURCE_DIR}/tests/states")
    set(pack "${CMAKE_CURRENT_SOURCE_DIR}/tests/pack")
    set(font "${storm_ui_rmlui_SOURCE_DIR}/Samples/assets/LatoLatin-Regular.ttf")
    # One host check: executable storm_ui_<name>_unit from tests/<source>, ctest ui_<name> with the given arguments.
    function(storm_ui_check name source)
        add_executable(storm_ui_${name}_unit tests/${source})
        target_link_libraries(storm_ui_${name}_unit PRIVATE storm_ui)
        target_include_directories(storm_ui_${name}_unit PRIVATE "${STORM_APP_CONTRACT_INCLUDE}")
        add_test(NAME ui_${name} COMMAND storm_ui_${name}_unit ${ARGN})
    endfunction()
    storm_ui_check(unit unit_check.cpp "${models}")
    storm_ui_check(perk_tree perk_tree_check.cpp)
    storm_ui_check(hud_lifecycle hud_lifecycle_check.cpp "${states}" "${CMAKE_BINARY_DIR}/app-pack")
    target_link_libraries(storm_ui_hud_lifecycle_unit PRIVATE storm_ui_script_game)
    add_dependencies(storm_ui_hud_lifecycle_unit storm_ui_app_pack)
    storm_ui_check(hud hud_check.cpp "${states}")
    storm_ui_check(hud_spyglass hud_spyglass_check.cpp "${states}")
    storm_ui_check(press press_check.cpp "${pack}" "${font}")
    target_link_libraries(storm_ui_press_unit PRIVATE storm_ui_script_game)
    storm_ui_check(launcher_hud launcher_hud_check.cpp "${pack}")
    storm_ui_check(display display_check.cpp "${pack}")
    storm_ui_check(slot slot_check.cpp "${pack}")

    # Reads an installed game ($STORM_GAME_DIR); without one it exits 77 and ctest reports it as not run.
    storm_ui_check(hud_land_game hud_land_game_check.cpp "${states}" "${CMAKE_BINARY_DIR}/app-pack")
    target_link_libraries(storm_ui_hud_land_game_unit PRIVATE storm_ui_script_game)
    add_dependencies(storm_ui_hud_land_game_unit storm_ui_app_pack)
    set_tests_properties(ui_hud_land_game PROPERTIES SKIP_RETURN_CODE 77)
    storm_ui_check(hud_sea hud_sea_check.cpp "${states}" "${pack}")
    storm_ui_check(frame_cost frame_cost_check.cpp "${states}" "${pack}" "${CMAKE_BINARY_DIR}/app-pack")
    target_link_libraries(storm_ui_frame_cost_unit PRIVATE storm_ui_script_game)
    add_dependencies(storm_ui_frame_cost_unit storm_ui_app_pack)
    target_include_directories(storm_ui_frame_cost_unit PRIVATE src)
    storm_ui_check(text text_check.cpp)
    storm_ui_check(boundary boundary_check.cpp)
    # The application's pack, for the checks of the script screens' components.
    file(GLOB_RECURSE storm_ui_app_design CONFIGURE_DEPENDS "${STORM_MOBILE_UI}/design/*")
    add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/app-pack/pack.json"
        COMMAND "${Python3_EXECUTABLE}" "${STORM_MOBILE_UI}/design/build.py" --pack "${CMAKE_BINARY_DIR}/app-pack"
                --engine-root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." DEPENDS ${storm_ui_app_design})
    add_custom_target(storm_ui_app_pack DEPENDS "${CMAKE_BINARY_DIR}/app-pack/pack.json")
    storm_ui_check(hud_land hud_land_check.cpp "${states}" "${CMAKE_BINARY_DIR}/app-pack")
    target_link_libraries(storm_ui_hud_land_unit PRIVATE storm_ui_script_game)
    add_dependencies(storm_ui_hud_land_unit storm_ui_app_pack)
    storm_ui_check(hud_worldmap hud_worldmap_check.cpp "${states}" "${CMAKE_BINARY_DIR}/app-pack")
    target_link_libraries(storm_ui_hud_worldmap_unit PRIVATE storm_ui_script_game)
    add_dependencies(storm_ui_hud_worldmap_unit storm_ui_app_pack)
    storm_ui_check(menus menus_check.cpp "${models}" "${CMAKE_BINARY_DIR}/app-pack")
    target_link_libraries(storm_ui_menus_unit PRIVATE storm_ui_script_game)
    add_dependencies(storm_ui_menus_unit storm_ui_app_pack)
    storm_ui_check(modals modals_check.cpp "${models}" "${pack}" "${font}" "${CMAKE_BINARY_DIR}/app-pack")
    target_link_libraries(storm_ui_modals_unit PRIVATE storm_ui_script_game)
    add_dependencies(storm_ui_modals_unit storm_ui_app_pack)
    storm_ui_check(rare rare_check.cpp "${models}" "${CMAKE_BINARY_DIR}/app-pack")
    target_link_libraries(storm_ui_rare_unit PRIVATE storm_ui_script_game)
    add_dependencies(storm_ui_rare_unit storm_ui_app_pack)
    storm_ui_check(screens screens_check.cpp "${models}" "${CMAKE_BINARY_DIR}/app-pack")
    target_link_libraries(storm_ui_screens_unit PRIVATE storm_ui_script_game)
    add_dependencies(storm_ui_screens_unit storm_ui_app_pack)
    storm_ui_check(taps taps_check.cpp "${models}" "${pack}" "${font}" "${STORM_MOBILE_UI}/design")
    target_link_libraries(storm_ui_taps_unit PRIVATE storm_ui_script_game)
    storm_ui_check(routing routing_check.cpp "${states}" "${CMAKE_BINARY_DIR}/app-pack")
    target_link_libraries(storm_ui_routing_unit PRIVATE storm_ui_script_game)
    add_dependencies(storm_ui_routing_unit storm_ui_app_pack)
    storm_ui_check(script_screen script_screen_check.cpp)
    # Shadows are drawn, not made: six panels with a shadow each (the first wider than the window) appear whole in
    # the very frame after the change, through the bgfx backend with few views, and no texture is created for them.
    if(STORM_UI_RENDER_BACKEND)
        add_test(NAME ui_whole_frame COMMAND storm_ui_shot --pack "${pack}" --doc panels.rml --size 640x360 --density 1
                 --views 12 --out "${CMAKE_CURRENT_BINARY_DIR}/whole-frame.png"
                 --script "wait 0.2; mark; props panels {\"sizes\":[640,50,60,70,80,90]}; arrived; made 0; whole")
        # The icon atlas's pages hold no CPU copy once uploaded; after a device loss they are decoded from the pack
        # again and the icons draw as before.
        add_test(NAME ui_atlas_device_loss COMMAND storm_ui_shot --pack "${pack}" --doc atlas.rml --size 320x120
                 --density 2 --out "${CMAKE_CURRENT_BINARY_DIR}/atlas-device-loss.png"
                 --script "wait 0.1; pack-bytes 0; mark; device-reset; wait 0.1; same; pack-bytes 0")
        # A faded icon's shadow does not show through its glyph: where the opaque glyph covers, a half-faded icon with a
        # shadow (blurred, hard, an accent icon's, a full-colour one's) is the half-faded icon without one.
        add_test(NAME ui_icon_fade_shadow COMMAND storm_ui_shot --pack "${pack}" --doc fade.rml --size 210x210 --density 2
                 --out "${CMAKE_CURRENT_BINARY_DIR}/icon-fade-shadow.png"
                 --script "wait 0.1; covered-same 10 10 10 60 10 110 10 160 40; covered-same 60 10 60 60 10 110 10 160 40; covered-same 110 10 110 60 110 110 110 160 40; covered-same 160 10 160 60 160 110 160 160 40")
        # A filter keeps the element's outer shadow (drawn by the element, outside RmlUi's ink overflow): below a
        # filtered panel the shadow is as below the same panel unfiltered.
        add_test(NAME ui_filtered_shadow COMMAND storm_ui_shot --pack "${pack}" --doc filtered.rml --size 200x100 --density 2
                 --out "${CMAKE_CURRENT_BINARY_DIR}/filtered-shadow.png"
                 --script "wait 0.1; covered-same 20 62 120 62 20 62 20 62 16; covered-same 64 62 164 62 64 62 64 62 16")
        # An unchanged frame composites the layer the last drawn one left: the touch opening the view (the clear of the
        # tool's target) and one quad, nothing drawn into the layer; a change draws the layer again.
        add_test(NAME ui_steady_frame COMMAND storm_ui_shot --pack "${pack}" --doc panels.rml --size 640x360 --density 1
                 --out "${CMAKE_CURRENT_BINARY_DIR}/steady-frame.png"
                 --script "wait 0.5; submitted 2 2; props panels {\"sizes\":[40,50,60]}; submitted 3 100000; wait 0.1; submitted 2 2")
    endif()
    if(STORM_UI_PACK)
        add_test(NAME ui_pack_contract COMMAND storm_ui_pack_check --pack "${STORM_UI_PACK}" --fixtures "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    endif()
endif()
