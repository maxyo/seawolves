# Effekseer effects on bgfx (effekseer/README.md): the pinned runtime and the pinned efkbgfx renderer with our patches.
#   storm_effekseer_runtime  Effekseer's core and EffekseerRendererCommon (pin "effekseer"; offline:
#                            -DFETCHCONTENT_SOURCE_DIR_STORM_EFFEKSEER=<extracted archive of the pin>)
#   storm_efkbgfx            efkbgfx's renderer with patches/efkbgfx-*.patch, its shaders embedded for both backends
#                            (pin "efkbgfx"; offline: -DFETCHCONTENT_SOURCE_DIR_STORM_EFKBGFX=<patched copy>)
include(FetchContent)
file(GLOB storm_efkbgfx_patches CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/patches/efkbgfx-*.patch")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${storm_efkbgfx_patches})
storm_fetch_pin(storm_effekseer effekseer STORM_POPULATE_ONLY)
storm_fetch_pin(storm_efkbgfx efkbgfx STORM_POPULATE_ONLY STORM_PATCHES . ${storm_efkbgfx_patches})
FetchContent_MakeAvailable(storm_effekseer storm_efkbgfx)
set(efk_dir "${storm_effekseer_SOURCE_DIR}/Dev/Cpp")

# The runtime without the editor's network server and without the default texture loader (effect textures go through
# the engine's texture cache, src/effekseer/textures.cpp); its PNG and TGA decoders stay (stb_image inside Effekseer).
file(GLOB_RECURSE efk_core_sources CONFIGURE_DEPENDS "${efk_dir}/Effekseer/Effekseer/*.cpp")
list(FILTER efk_core_sources EXCLUDE REGEX "/Network/")
file(GLOB efk_common_sources CONFIGURE_DEPENDS "${efk_dir}/EffekseerRendererCommon/EffekseerRendererCommon/*.cpp")
list(FILTER efk_common_sources EXCLUDE REGEX "/(TextureLoader|EffekseerRenderer\\.DDSTextureLoader)\\.cpp$")
add_library(storm_effekseer_runtime STATIC ${efk_core_sources} ${efk_common_sources})
target_include_directories(storm_effekseer_runtime SYSTEM PUBLIC
    "${efk_dir}/Effekseer" "${efk_dir}/EffekseerRendererCommon")
target_compile_features(storm_effekseer_runtime PUBLIC cxx_std_17)
# Upstream code: its warnings are not ours to fix.
target_compile_options(storm_effekseer_runtime PRIVATE -w)
find_package(Threads REQUIRED)
target_link_libraries(storm_effekseer_runtime PUBLIC Threads::Threads)
set_target_properties(storm_effekseer_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)

# efkbgfx's shaders, one group per varying file. A sprite group takes the fragment shader of its model group.
set(efk_shader_dir "${storm_efkbgfx_SOURCE_DIR}/shaders")
set(efk_shader_targets)
foreach(kind unlit lit distortion)
    if(kind STREQUAL "unlit")
        set(varying_kind Unlit)
    elseif(kind STREQUAL "lit")
        set(varying_kind Lit)
    else()
        set(varying_kind BackDistortion)
    endif()
    # The renderer's names (sprite_adv_lit, ...) for upstream's files (ad_sprite_lit_vs.fx.sc, ...).
    foreach(group sprite sprite_adv model model_adv)
        string(REGEX REPLACE "_adv$" "" geometry "${group}")
        set(file "${geometry}_${kind}")
        set(varying "${geometry}_${varying_kind}")
        if(group MATCHES "_adv$")
            set(file "ad_${geometry}_${kind}")
            set(varying "${geometry}_Advanced${varying_kind}")
        endif()
        set(jobs "vs_efk_${group}_${kind}|${efk_shader_dir}/${file}_vs.fx.sc")
        if(geometry STREQUAL "model")
            list(APPEND jobs "fs_efk_${group}_${kind}|${efk_shader_dir}/${file}_ps.fx.sc")
        endif()
        storm_bgfx_embed_shaders(TARGET storm_efk_shaders_${group}_${kind} NAMESPACE efk_${group}_${kind}
            VARYING "${efk_shader_dir}/${varying}_varying.def.sc" VARIANTS ${jobs} INCLUDES "${efk_shader_dir}")
        list(APPEND efk_shader_targets storm_efk_shaders_${group}_${kind})
    endforeach()
endforeach()

add_library(storm_efkbgfx STATIC "${storm_efkbgfx_SOURCE_DIR}/renderer/bgfxrenderer.cpp" src/shaders.cpp)
target_include_directories(storm_efkbgfx PUBLIC "${storm_efkbgfx_SOURCE_DIR}/renderer" src
    PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../include")
target_link_libraries(storm_efkbgfx PUBLIC storm_effekseer_runtime storm_bgfx_runtime PRIVATE ${efk_shader_targets})
target_compile_features(storm_efkbgfx PUBLIC cxx_std_20)
set_target_properties(storm_efkbgfx PROPERTIES POSITION_INDEPENDENT_CODE ON)
