# storm_relief_model_digests(<include dir>): <include dir>/storm/fx/model_digests.hpp, the first 8 bytes of each ncnn
# graph's .bin sha256 as pinned in engine/pins.json (pbrify-*: ncnn_bin_sha256), for fx::kRelief*ModelDigest and
# fx::kUpscaleModelDigest (part of the maps' and the ×2's methods). Requires storm_pin (cmake/StormBuildCache.cmake).
function(storm_relief_model_digests directory)
    foreach(model normal height roughness upscale)
        storm_pin(pbrify-${model} ncnn_bin_sha256 bin)
        string(SUBSTRING "${bin}" 0 16 STORM_DIGEST_${model})
    endforeach()
    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/model_digests.hpp.in" "${directory}/storm/fx/model_digests.hpp"
        @ONLY)
endfunction()
