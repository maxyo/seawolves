# Host checks of the bgfx renderer: storm_bgfx (shim, shaders compiled by the pinned shaderc) and the relief
# preparation built as the engine builds them (optimised, NDEBUG), the checks that draw on OpenGL ES in an SDL window
# (an X display: xvfb-run when headless) and again on Vulkan, the texture-lifetime checks again with the renderer's
# own code under AddressSanitizer (a second build tree, configured from this directory alone: STORM_CHECK_ASAN).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.28)
    project(storm_renderer_bgfx_asan_checks LANGUAGES C CXX)
    include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/StormBuildCache.cmake")
    get_filename_component(STORM_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
    set(src "${STORM_ROOT}/src")
    set(STORM_TEST_SDL_INCLUDE "" CACHE PATH "directory that contains SDL2/SDL.h")
    set(STORM_TEST_SOURCES "" CACHE PATH "the pinned engine dependencies (spdlog, fmt) of the host checks")
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
endif()
option(STORM_CHECK_ASAN "the renderer's code and the lifetime checks with AddressSanitizer (the second tree)" OFF)
set(CMAKE_C_FLAGS "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS "-O3 -DNDEBUG")
set(CMAKE_CXX_EXTENSIONS ON)
set_property(DIRECTORY PROPERTY LABELS renderer)

add_library(storm_check_sdl INTERFACE)
target_include_directories(storm_check_sdl INTERFACE "${STORM_TEST_SDL_INCLUDE}")
# The pinned SDL headers carry no platform configuration; the checks run on the host's X11 SDL runtime, so the shim
# takes its native window from SDL's X11 window info.
find_library(STORM_SDL_RUNTIME NAMES SDL2-2.0 SDL2 libSDL2-2.0.so.0 REQUIRED)
target_link_libraries(storm_check_sdl INTERFACE "${STORM_SDL_RUNTIME}")
set(SDL2_LIBRARIES storm_check_sdl)
set(STORM_BGFX_BUILD_TESTS ON CACHE BOOL "" FORCE)
# The renderer's log (renderer_bgfx/log) over the pinned spdlog and fmt, header-only (cmake/spdlog.cmake uses it).
add_library(spdlog INTERFACE)
target_include_directories(spdlog SYSTEM INTERFACE "${STORM_TEST_SOURCES}/spdlog/include" "${STORM_TEST_SOURCES}/fmt/include")
target_compile_definitions(spdlog INTERFACE SPDLOG_FMT_EXTERNAL FMT_HEADER_ONLY)
add_subdirectory("${src}/libs/renderer_bgfx" renderer_bgfx)
add_subdirectory("${src}/libs/relief" relief)
target_compile_definitions(storm_bgfx PRIVATE SDL_VIDEO_DRIVER_X11=1)

# The checks with the renderer's code under AddressSanitizer: a texture the game frees while the effects still know it
# (lamp caches, cut-out casters, the town light's lightmaps) must not be read. On OpenGL ES only: the lifetimes do not
# depend on the backend. bgfx, ncnn and the other third-party libraries stay uninstrumented.
set(asan_checks cutout_caster_check lamp_lifetime_check scene_frame_check sun_relight_check)
if(STORM_CHECK_ASAN)
    foreach(target storm_bgfx storm_bgfx_fx storm_bgfx_fx_core storm_texture_core)
        target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    endforeach()
endif()
set(asan_tree "${CMAKE_BINARY_DIR}/renderer-asan")
set(shim "${src}/libs/renderer_bgfx/src/shim")
set(renderer "${src}/libs/renderer_bgfx/src")
set(model ${src}/libs/model/src ${src}/libs/model/include)

# storm_renderer_check(<check> [GPU] [GLES_ONLY] [VULKAN_ONLY] [ASAN] [CATCH2] [INCLUDES <dir>...] [ENV <VAR=value>...]
#                      [ARGS <arg>...] [OPTIONS <flag>...])
# The program <check> (<check>.cpp) linked with storm_bgfx and the SDL runtime, and its test renderer_bgfx.<check>.
# GPU: it draws (label gpu, an X display) on OpenGL ES, and as renderer_bgfx.<check>.vulkan on Vulkan unless GLES_ONLY
# (VULKAN_ONLY: only there). A Vulkan run exits 77 on a host without a Vulkan device (check_backend.hpp) and fails if
# it ended on OpenGL ES (VULKAN_FALLBACK: the check provokes that fallback itself and checks each backend it gets).
# ASAN: also renderer_bgfx.<check>.asan, the program of the ASan tree. CATCH2: its cases run by the shared Catch2 main.
function(storm_renderer_check check)
    cmake_parse_arguments(PARSE_ARGV 1 arg "GPU;GLES_ONLY;VULKAN_ONLY;VULKAN_FALLBACK;ASAN;CATCH2" "" "INCLUDES;ENV;ARGS;OPTIONS")
    add_executable(${check} ${check}.cpp)
    target_include_directories(${check} PRIVATE ${shim} ${arg_INCLUDES})
    target_compile_definitions(${check} PRIVATE NOMINMAX)
    target_compile_options(${check} PRIVATE -fsigned-char ${arg_OPTIONS})
    target_link_libraries(${check} PRIVATE storm_bgfx storm_check_sdl)
    if(arg_CATCH2)
        target_link_libraries(${check} PRIVATE storm_catch2_main)
    endif()
    if(STORM_CHECK_ASAN)
        if(arg_ASAN)
            target_compile_options(${check} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
            target_link_options(${check} PRIVATE -fsanitize=address)
        endif()
        return()
    endif()
    set(labels "")
    if(arg_GPU)
        set(labels gpu display)
    endif()
    if(NOT arg_VULKAN_ONLY)
        storm_check_test(renderer_bgfx.${check} COMMAND ${check} ${arg_ARGS} ENV ${arg_ENV} LABELS ${labels})
    endif()
    if(arg_GPU AND NOT arg_GLES_ONLY)
        storm_check_test(renderer_bgfx.${check}.vulkan COMMAND ${check} ${arg_ARGS}
            ENV STORM_CHECK_BACKEND=vulkan ${arg_ENV} LABELS ${labels} vulkan)
        if(NOT arg_VULKAN_FALLBACK)
            set_tests_properties(renderer_bgfx.${check}.vulkan PROPERTIES
                FAIL_REGULAR_EXPRESSION "render backend: OpenGL ES \\(render_backend = vulkan\\)")
        endif()
    endif()
    if(arg_ASAN)
        storm_check_test(renderer_bgfx.${check}.asan COMMAND "${asan_tree}/${check}" ${arg_ARGS}
            ENV ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 ${arg_ENV} LABELS ${labels} asan)
        set_tests_properties(renderer_bgfx.${check}.asan PROPERTIES FIXTURES_REQUIRED "storm_tmp;renderer_asan")
    endif()
endfunction()

# Without a GPU: bgfx's Noop backend or none.
storm_renderer_check(perf_check)
storm_renderer_check(program_table_check CATCH2 INCLUDES ${renderer})
storm_renderer_check(texture_maps_switch_check CATCH2 INCLUDES ${renderer})
storm_renderer_check(view_schedule_check)
storm_renderer_check(backend_select_check)
storm_renderer_check(lamp_lifetime_check INCLUDES ${renderer} ASAN)
storm_renderer_check(relief_switch_check INCLUDES ${renderer})
storm_renderer_check(realize_budget_check OPTIONS -UNDEBUG)
storm_renderer_check(diagnostics_tier_check INCLUDES ${renderer})
storm_renderer_check(view_to_volume_check INCLUDES ${renderer})
storm_renderer_check(skin_sampling_check INCLUDES ${renderer})
storm_renderer_check(skin_tone_bind_check INCLUDES ${renderer})
storm_renderer_check(scene_frame_check INCLUDES ${renderer} ASAN)
storm_renderer_check(skin_reference_check INCLUDES ${renderer})
storm_renderer_check(sea_depth_held_check INCLUDES ${renderer})
storm_renderer_check(app_settings_check INCLUDES ${STORM_APP_CONTRACT_INCLUDE})
storm_renderer_check(skin_weights_check INCLUDES ${model} ${src}/libs/renderer/include ${src}/libs/core/include ${src}/libs/util/include ${src}/libs/math/include ${src}/libs/shared_headers/include)
# They draw, on OpenGL ES and on Vulkan.
storm_renderer_check(skinning_check GPU INCLUDES ${model})
storm_renderer_check(grass_vertex_check GPU)
# The sea's surface against the wave field itself (src/libs/sea/src/ocean_*, with the engine's FFmpeg): not in the ASan
# tree, which is configured from this folder alone.
if(NOT STORM_CHECK_ASAN)
    foreach(check coast_foam_vertex_check sea_surface_vertex_check)
        storm_renderer_check(${check} GPU INCLUDES ${src}/libs/sea/src ${src}/libs/sea/include ${src}/libs/island/src)
        target_sources(${check} PRIVATE ${src}/libs/sea/src/ocean_fft.cpp ${src}/libs/sea/src/ocean_spectrum.cpp
                                        ${src}/libs/sea/src/ocean_field.cpp)
        target_link_libraries(${check} PRIVATE storm_ffmpeg)
    endforeach()
endif()
storm_renderer_check(point_sprite_check GPU)
storm_renderer_check(alpha_test_check GPU)
storm_renderer_check(ff_stage_check GPU)
storm_renderer_check(ff_specialised_check GPU CATCH2 INCLUDES ${renderer})
storm_renderer_check(preset_switch_check GPU INCLUDES ${renderer})
storm_renderer_check(sea_edge_check GPU INCLUDES ${renderer})
storm_renderer_check(sea_depth_capture_check GPU)
storm_renderer_check(program_cache_check GPU INCLUDES ${renderer})
storm_renderer_check(render_thread_check GPU VULKAN_FALLBACK INCLUDES ${renderer})
storm_renderer_check(surface_lifecycle_check GPU INCLUDES ${renderer})
storm_renderer_check(ao_volume_check GPU INCLUDES ${renderer})
storm_renderer_check(cutout_caster_check GPU ASAN)
storm_renderer_check(lamp_shadow_check GPU INCLUDES ${renderer})
storm_renderer_check(shadow_casters_check GPU INCLUDES ${renderer} ${model})
storm_renderer_check(sun_layers_check GPU INCLUDES ${renderer})
storm_renderer_check(sun_relight_check GPU INCLUDES ${renderer} ASAN)
storm_renderer_check(sky_clouds_check GPU INCLUDES ${renderer})
storm_renderer_check(texture_format_check GPU INCLUDES ${renderer})
storm_renderer_check(upscale_swap_check GPU INCLUDES ${renderer})
storm_renderer_check(buffer_pool_check GPU)
storm_renderer_check(thumbnail_check GPU)
storm_renderer_check(pipeline_prewarm_check GPU INCLUDES ${renderer})
storm_renderer_check(pipeline_stress_check GPU INCLUDES ${renderer})
storm_renderer_check(background_stay_check GPU CATCH2 INCLUDES ${renderer} ${src}/libs/util/include)
storm_renderer_check(start_memory_check CATCH2 INCLUDES ${renderer} ${src}/libs/util/include)
storm_renderer_check(wet_surfaces_check GPU)
# The sample effects of the pinned efkbgfx sources (MIT) drawn through the shim.
FetchContent_GetProperties(storm_efkbgfx)
storm_renderer_check(effekseer_check GPU CATCH2 ENV "STORM_EFFEKSEER_SAMPLES=${storm_efkbgfx_SOURCE_DIR}/examples/resources")
storm_renderer_check(materials_check GPU CATCH2 INCLUDES ${renderer})
# A game's real models with their materials, when STORM_GAME_DIR names a game that ships them.
storm_renderer_check(game_materials_check GPU CATCH2 INCLUDES ${renderer} ${src}/libs/geometry/src)
target_link_libraries(game_materials_check PRIVATE scene_data)
if(NOT STORM_CHECK_ASAN) # the ASan tree creates no test of it
    set_tests_properties(renderer_bgfx.game_materials_check renderer_bgfx.game_materials_check.vulkan PROPERTIES
        LABELS "gpu;display;game" SKIP_RETURN_CODE 77 TIMEOUT 900)
endif()
# It counts the GL backend's glUniform calls (patches/bgfx-gl-uniform-commit.patch).
storm_renderer_check(uniform_commit_check GPU GLES_ONLY)
# bgfx's Vulkan backend resolves in the render pass (patches/bgfx-vulkan-resolve-in-pass.patch).
storm_renderer_check(msaa_resolve_check GPU VULKAN_ONLY)
# The scene's render passes and their traffic on Vulkan (patches/bgfx-vulkan-traffic-counters.patch).
storm_renderer_check(scene_passes_check GPU VULKAN_ONLY INCLUDES ${renderer} ENV STORM_VK_PIPELINE_STATISTICS=1)
# The depth pre-pass; the drivers' own shader caches off, so a new pipeline is created, not found.
storm_renderer_check(depth_prepass_check GPU INCLUDES ${renderer} ${model} ENV STORM_VK_PIPELINE_STATISTICS=1
    MESA_SHADER_CACHE_DISABLE=true __GL_SHADER_DISK_CACHE=0)
# Needs EXT_multisampled_render_to_texture, which Mesa's llvmpipe lacks: on the host GPU through VirtualGL when
# STORM_VIRTUALGL is set (as the scene sweep and the boot stage use), else it exits 77.
set(msrtt_env "")
if(STORM_VIRTUALGL)
    set(msrtt_env VGL_DISPLAY=egl
        "LD_PRELOAD=${STORM_VIRTUALGL}/usr/lib/libdlfaker.so:${STORM_VIRTUALGL}/usr/lib/libvglfaker.so")
endif()
storm_renderer_check(msrtt_reuse_check GPU GLES_ONLY ENV ${msrtt_env})

# The texture swap fade's state (src/shim/texture_fade.hpp): no renderer.
add_executable(texture_fade_check texture_fade_check.cpp)
target_include_directories(texture_fade_check PRIVATE ${shim})
target_compile_options(texture_fade_check PRIVATE -UNDEBUG)
# The producers' state chains, the worker/cache retry and the scene textures' failures, on the Noop backend.
foreach(pair IN ITEMS "scene_resources_check;resources" "scene_retry_check;retry"
                  "scene_texture_failure_check;texture_failure")
    list(GET pair 0 target)
    list(GET pair 1 source)
    add_executable(${target} "${STORM_ROOT}/tests/scene_readiness_check/${source}.cpp")
    target_include_directories(${target} PRIVATE ${renderer})
    target_compile_options(${target} PRIVATE -UNDEBUG)
    target_link_libraries(${target} PRIVATE storm_bgfx storm_check_sdl)
endforeach()

if(STORM_CHECK_ASAN)
    return()
endif()

storm_check_test(renderer_bgfx.texture_fade_check COMMAND texture_fade_check)
# scene_passes_check with the scene's depth resolved by bgfx's pass of its own, as on a Vulkan device without
# VK_KHR_depth_stencil_resolve.
storm_check_test(renderer_bgfx.scene_passes_check.vulkan_resolve_pass COMMAND scene_passes_check
    ENV STORM_CHECK_BACKEND=vulkan STORM_VK_PIPELINE_STATISTICS=1 STORM_VK_DEPTH_RESOLVE_IN_PASS=0
    LABELS gpu display vulkan)
set_tests_properties(renderer_bgfx.scene_passes_check.vulkan_resolve_pass PROPERTIES
    FAIL_REGULAR_EXPRESSION "render backend: OpenGL ES \\(render_backend = vulkan\\)")
# Every effect of the installed game in $STORM_GAME_DIR (Caribbean Legend's Resource/Effects).
storm_check_test(renderer_bgfx.effekseer_check.corpus COMMAND effekseer_check "[corpus]"
    ENV "STORM_EFFEKSEER_SAMPLES=${storm_efkbgfx_SOURCE_DIR}/examples/resources" LABELS gpu display game)
# The scene's targets, the effects' self-test and the sea's backdrop as on a device that renders to no D24S8
# (no_d24s8.hpp).
foreach(check preset_switch_check sea_edge_check)
    storm_check_test(renderer_bgfx.${check}.no_d24s8 COMMAND ${check} ENV STORM_CHECK_NO_D24S8=1 LABELS gpu display)
    storm_check_test(renderer_bgfx.${check}.no_d24s8.vulkan COMMAND ${check}
        ENV STORM_CHECK_BACKEND=vulkan STORM_CHECK_NO_D24S8=1 LABELS gpu display vulkan)
    set_tests_properties(renderer_bgfx.${check}.no_d24s8.vulkan PROPERTIES
        FAIL_REGULAR_EXPRESSION "render backend: OpenGL ES \\(render_backend = vulkan\\)")
endforeach()
# The "gpu passes:" line's sums (src/shim/gpu_passes.hpp) from synthetic bgfx statistics: no renderer.
add_executable(gpu_passes_check gpu_passes_check.cpp)
target_include_directories(gpu_passes_check PRIVATE ${shim})
target_link_libraries(gpu_passes_check PRIVATE storm_bgfx storm_check_sdl storm_catch2_main)
storm_check_test(renderer_bgfx.gpu_passes_check COMMAND gpu_passes_check)
foreach(target scene_resources_check scene_retry_check scene_texture_failure_check)
    storm_check_test(renderer_bgfx.${target} COMMAND ${target})
endforeach()
# The relief preparation: the real models on a small synthetic game (and the CPU networks' memory bound on the game's
# largest x2 and relief jobs), the device choice, the finalization and the .tx reader.
set(relief_models "${CMAKE_BINARY_DIR}/relief_models")
storm_check_test(renderer_bgfx.relief_check COMMAND storm_relief_check ${relief_models})
storm_check_test(renderer_bgfx.relief_check.memory COMMAND storm_relief_check ${relief_models} --memory)
storm_check_test(renderer_bgfx.relief_device_choice_check COMMAND storm_relief_device_choice_check <tmp>)
storm_check_test(renderer_bgfx.relief_finalizer_check COMMAND storm_relief_finalizer_check <tmp>)
storm_check_test(renderer_bgfx.relief_tx_check COMMAND storm_relief_tx_check <tmp>)
# The tests the renderer's own CMake files declare (texture transcode, fx).
set_tests_properties(storm_texture_tests storm_eac_encoder_tests storm_texture_file_tests
    DIRECTORY "${src}/libs/renderer_bgfx" PROPERTIES LABELS "host" TIMEOUT 300 SKIP_RETURN_CODE 77)
set_tests_properties(storm_texture_file_tests.corpus DIRECTORY "${src}/libs/renderer_bgfx"
    PROPERTIES LABELS "host;game" TIMEOUT 600 SKIP_RETURN_CODE 77)
set_tests_properties(fx_core fx_aggregate fx_wake_trail fx_sea_wakes_math fx_ff_skin_share
    DIRECTORY "${src}/libs/renderer_bgfx/fx" PROPERTIES LABELS "host" TIMEOUT 300 SKIP_RETURN_CODE 77)
# The shaders as built: the vertex inputs of the SPIR-V programs, BuiltIn PointSize exported and stored, no texel of a
# fragment program taken through OpSelect, no stage uniform read by a specialised program.
set(spirv_reader "${src}/libs/renderer_bgfx/cmake/spirv_blob.py")
storm_script_check(renderer_bgfx.vertex_input_spirv vertex_input_spirv_check.py
    "${CMAKE_CURRENT_BINARY_DIR}/renderer_bgfx/shaders-gen/storm_shaders" INPUTS ${spirv_reader})
storm_script_check(renderer_bgfx.point_size_spirv point_size_spirv_check.py
    --shaderc "${STORM_SHADERC_COMMAND}" --bgfx-include "${STORM_BGFX_SOURCE_DIR}/src" INPUTS ${spirv_reader})
storm_script_check(renderer_bgfx.fragment_spirv fragment_spirv_check.py
    "${CMAKE_CURRENT_BINARY_DIR}/renderer_bgfx/shaders-gen/storm_shaders" INPUTS ${spirv_reader})
storm_script_check(renderer_bgfx.specialised_spirv specialised_spirv_check.py
    "${CMAKE_CURRENT_BINARY_DIR}/renderer_bgfx/shaders-gen/storm_shaders"
    "${CMAKE_CURRENT_BINARY_DIR}/renderer_bgfx/generated/shaders/ff_specialised.txt" INPUTS ${spirv_reader})

# The ASan tree, configured and built when an ASan check runs. RUN_SERIAL: the build takes every core, and beside it
# the checks that wait on worker threads (pipeline_stress_check on lavapipe) starve on a 4-core runner.
string(JOIN "," asan_list ${asan_checks})
storm_check_test(renderer_bgfx.asan_build COMMAND "${CMAKE_COMMAND}" -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}
        -DBINARY=${asan_tree} -DSDL_INCLUDE=${STORM_TEST_SDL_INCLUDE} -DSOURCES=${STORM_TEST_SOURCES}
        "-DTARGETS=${asan_list}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/asan_tree.cmake"
    LABELS asan TIMEOUT 1800 RUN_SERIAL)
set_tests_properties(renderer_bgfx.asan_build PROPERTIES FIXTURES_SETUP renderer_asan)
