# Graphics effects package for the bgfx renderer (see README.md).
#
# This directory is self-contained and is NOT picked up by the engine build on
# its own: the renderer adds it with add_subdirectory()
# after bgfx exists. Required from the parent scope:
#   target   storm_bgfx_runtime (bgfx, bx, bimg)
#   STORM_FX_SHADERC_COMMAND       host shaderc built from the pinned bgfx
#   STORM_FX_SHADERC_DEPENDS       target/file the shader build depends on
#   STORM_FX_BGFX_SHADER_INCLUDE   <bgfx>/src (bgfx_shader.sh)
# render/bgfx can pass its STORM_SHADERC_COMMAND / STORM_SHADERC_DEPENDS and
# "${STORM_BGFX_SOURCE_DIR}/src" unchanged.
cmake_minimum_required(VERSION 3.21)
if(NOT TARGET storm_bgfx_runtime)
    message(FATAL_ERROR "renderer_bgfx/fx needs the pinned bgfx (storm_bgfx_runtime); add it from renderer_bgfx")
endif()
set(STORM_FX_SHADER_PROFILE "300_es" CACHE STRING "shaderc profile for the embedded fx shader blobs")
include(cmake/storm_fx_shaders.cmake)

# madmann91/bvh (MIT, header-only, C++20): the bounding volume hierarchy of the place's ray bakes (src/sun_fit.cpp
# Occluders). Offline: -DFETCHCONTENT_SOURCE_DIR_STORM_BVH=/path/to/this/revision
include(FetchContent)
storm_fetch_pin(storm_bvh bvh STORM_POPULATE_ONLY)
# SMAA (MIT): the reference SMAA.hlsl (included by shaders/include/fx_smaa.sh) and its precomputed area and search
# tables (Textures/AreaTex.h, SearchTex.h, uploaded by src/post_chain.cpp). Offline: -DFETCHCONTENT_SOURCE_DIR_STORM_SMAA=...
storm_fetch_pin(storm_smaa smaa STORM_POPULATE_ONLY)
# AMD FidelityFX Super Resolution 1 (MIT): ffx-fsr/ffx_a.h and ffx_fsr1.h, the EASU / RCAS pixel shaders
# (shaders/include/fx_fsr.sh) and their constant setup on the CPU (src/post_chain.cpp).
# Offline: -DFETCHCONTENT_SOURCE_DIR_STORM_FIDELITYFX_FSR=...
storm_fetch_pin(storm_fidelityfx_fsr fidelityfx-fsr STORM_POPULATE_ONLY)
FetchContent_MakeAvailable(storm_bvh storm_smaa storm_fidelityfx_fsr)
include(shaders/shaders.cmake)

add_library(storm_bgfx_fx_core STATIC
    src/color_lut.cpp src/sky_model.cpp src/cloud_cubes.cpp
    src/lighting_model.cpp src/light_falloff.cpp src/light_set.cpp src/shadow_model.cpp src/normal_map.cpp src/settings.cpp
    src/ao_volume.cpp src/lamp_shadows.cpp src/bakes.cpp src/sun_fit.cpp src/town_light.cpp src/visibility.cpp src/place_probes.cpp src/relief_model.cpp src/relief_store.cpp src/gloss_map.cpp src/upscale.cpp
    src/height_map.cpp src/parallax_trust.cpp src/parallax_exclusions.cpp src/cutout.cpp src/wetness.cpp src/gpu_crash_guard.cpp
    src/skin_tone.cpp)
target_compile_features(storm_bgfx_fx_core PUBLIC cxx_std_20)
target_include_directories(storm_bgfx_fx_core PUBLIC include PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../util/include") # fnv_hash.hpp
target_include_directories(storm_bgfx_fx_core SYSTEM PRIVATE "${storm_bvh_SOURCE_DIR}/src")
# storm/fx/model_digests.hpp: the relief and ×2 models' digests, from their pins.
include("${CMAKE_CURRENT_LIST_DIR}/../../relief/model/digests.cmake")
storm_relief_model_digests("${CMAKE_CURRENT_BINARY_DIR}/model_digests")
target_include_directories(storm_bgfx_fx_core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/model_digests")
find_package(Threads REQUIRED)
target_link_libraries(storm_bgfx_fx_core PUBLIC Threads::Threads) # the bake pool
target_link_libraries(storm_bgfx_fx_core PUBLIC storm_render_log storm_xxhash) # fx::Hasher (hash.hpp)
# GLM through the engine's storm_glm.h, which holds its configuration (src/libs/math/README.md).
include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/glm.cmake")
target_include_directories(storm_bgfx_fx_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../../math/include")
target_link_libraries(storm_bgfx_fx_core PUBLIC glm::glm-header-only)
set_target_properties(storm_bgfx_fx_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(storm_bgfx_fx STATIC
    src/gpu_util.cpp src/post_chain.cpp src/sea_depth.cpp src/sea_wakes.cpp src/sky.cpp src/lighting.cpp src/shadows.cpp src/sun_relight.cpp src/capabilities.cpp
    src/self_test.cpp)
target_link_libraries(storm_bgfx_fx PUBLIC storm_bgfx_fx_core storm_bgfx_runtime PRIVATE storm_fx_shaders)
target_include_directories(storm_bgfx_fx SYSTEM PRIVATE "${storm_smaa_SOURCE_DIR}/Textures" "${storm_fidelityfx_fsr_SOURCE_DIR}/ffx-fsr")
# storm/render/shader_blob.hpp (header only): the embedded blob of the running backend; also for the tools that
# embed their own shaders with storm_fx_build_shaders.
target_include_directories(storm_bgfx_fx PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../include")
set_target_properties(storm_bgfx_fx PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_CROSSCOMPILING)
    add_executable(storm_fx_core_check tests/core_check.cpp)
    target_link_libraries(storm_fx_core_check PRIVATE storm_bgfx_fx_core)
    add_test(NAME fx_core COMMAND storm_fx_core_check)
    add_executable(storm_fx_aggregate_check "${CMAKE_CURRENT_LIST_DIR}/../../../../tests/renderer_bgfx/aggregate_light_check.cpp")
    target_link_libraries(storm_fx_aggregate_check PRIVATE storm_bgfx_fx_core)
    add_test(NAME fx_aggregate COMMAND storm_fx_aggregate_check)
    # Header-only checks of the wake code and of shader arithmetic the CPU checks share with the GPU.
    foreach(check wake_trail sea_wakes_math ff_skin_share)
        add_executable(storm_fx_${check}_check tests/${check}_check.cpp)
        target_compile_features(storm_fx_${check}_check PRIVATE cxx_std_20)
        target_include_directories(storm_fx_${check}_check PRIVATE include)
        add_test(NAME fx_${check} COMMAND storm_fx_${check}_check)
    endforeach()
endif()
