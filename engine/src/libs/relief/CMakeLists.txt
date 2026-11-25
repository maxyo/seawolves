# Relief preparation: PBRify's models through ncnn, on the GPU or the CPU. The game makes its maps with them
# while it runs (renderer_bgfx effects/relief_generator).
include(FetchContent)

# PBRify's normal, height and roughness models (CC0-1.0): not in the repository. The float models are fetched by their
# pins (engine/pins.json pbrify-*: the float model and the sha256 of the ncnn graphs made of it);
# model/make_ncnn_models.py converts them to ncnn graphs at build time, once per machine (storm_derived_files,
# cmake/StormDerivedFiles.cmake), and the results must be byte for byte the pinned ones. The maps' method includes the
# weights' digests (model/digests.cmake): a new model changes them.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/StormDerivedFiles.cmake")
set(storm_relief_sources "")
set(storm_relief_inputs "")
set(storm_relief_arguments "")
set(storm_relief_pins "")
set(storm_relief_ncnn_files "")
foreach(model normal height roughness)
    # The ONNX exports of PBRify_Remix's models by fernandotonon/QtMeshEditor-models (third_party/licenses/PBRify-*).
    storm_fetch_pin(storm_pbrify_${model} pbrify-${model} DOWNLOAD_NO_EXTRACT TRUE STORM_POPULATE_ONLY)
    FetchContent_MakeAvailable(storm_pbrify_${model})
    storm_pin(pbrify-${model} version file)
    storm_pin(pbrify-${model} sha256 sha256)
    storm_pin(pbrify-${model} ncnn_param_sha256 param)
    storm_pin(pbrify-${model} ncnn_bin_sha256 bin)
    list(APPEND storm_relief_sources "${storm_pbrify_${model}_SOURCE_DIR}")
    list(APPEND storm_relief_inputs "${sha256}")
    list(APPEND storm_relief_arguments --model "pbrify-${model}=${file}.onnx=${sha256}")
    list(APPEND storm_relief_pins "pbrify-${model}.ncnn.param=${param}" "pbrify-${model}.ncnn.bin=${bin}")
    list(APPEND storm_relief_ncnn_files "pbrify-${model}.ncnn.param" "pbrify-${model}.ncnn.bin")
endforeach()
storm_derived_files(storm_relief_ncnn_models storm_relief_ncnn_models
    SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/model/make_ncnn_models.py"
    REQUIREMENTS "${CMAKE_CURRENT_SOURCE_DIR}/model/requirements-ncnn.txt"
    SOURCES ${storm_relief_sources}
    INPUTS ${storm_relief_inputs}
    OUTPUTS ${storm_relief_pins}
    ARGS ${storm_relief_arguments})
# PBRify's 4x upscaler (CC0-1.0, fx/upscale.hpp): model/make_upscale_model.py exports the release's
# .pth with spandrel and converts it by pnnx to an ncnn graph, but those bytes depend on the CPU's ISA (an AVX2 machine
# makes other ones than the AVX-512 reference machine). So the reference machine's two files are kept in model/ and
# checked here by sha256; the host target storm_upscale_model_check makes them again with the script and fails unless
# every file matches its pin (run it on the reference machine after changing the script, its requirements or the
# release's .pth, then replace the files and change the pins of pbrify-upscale in engine/pins.json).
storm_pin(pbrify-upscale ncnn_param_sha256 param)
storm_pin(pbrify-upscale ncnn_bin_sha256 bin)
set(storm_upscale_files "pbrify-upscale.ncnn.param|${param}" "pbrify-upscale.ncnn.bin|${bin}")
set(storm_upscale_copies "")
set(storm_upscale_pins "")
foreach(entry ${storm_upscale_files})
    string(REPLACE "|" ";" fields "${entry}")
    list(GET fields 0 file)
    list(GET fields 1 sha256)
    set(model_file "${CMAKE_CURRENT_SOURCE_DIR}/model/${file}")
    file(SHA256 "${model_file}" actual)
    if(NOT actual STREQUAL sha256)
        message(FATAL_ERROR "${model_file} is not the pinned upscaler model file (sha256 ${actual}, expected ${sha256})")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${model_file}")
    list(APPEND storm_upscale_copies "${model_file}")
    list(APPEND storm_upscale_pins "${file}=${sha256}")
endforeach()
if(NOT ANDROID)
    storm_fetch_pin(storm_pbrify_upscaler pbrify-upscale STORM_POPULATE_ONLY)
    storm_pin(pbrify-upscale sha256 storm_upscale_source_sha256)
    FetchContent_MakeAvailable(storm_pbrify_upscaler)
    storm_derived_files(storm_upscale_model_check storm_upscale_model
        SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/model/make_upscale_model.py"
        REQUIREMENTS "${CMAKE_CURRENT_SOURCE_DIR}/model/upscale-requirements.txt"
        SOURCES "${storm_pbrify_upscaler_SOURCE_DIR}"
        INPUTS ${storm_upscale_source_sha256}
        OUTPUTS ${storm_upscale_pins}
        PIP_ARGS --index-url https://download.pytorch.org/whl/cpu --extra-index-url https://pypi.org/simple)
endif()
# The graphs in one folder, as the game reads them (relief.hpp kModelFiles): <build>/game/assets on Android (packaged),
# <build>/relief_models on the host (engine.ini relief_models, the checks).
if(ANDROID)
    set(STORM_RELIEF_MODELS "${CMAKE_BINARY_DIR}/game/assets")
else()
    set(STORM_RELIEF_MODELS "${CMAKE_BINARY_DIR}/relief_models")
endif()
set(storm_relief_copies "")
foreach(file ${storm_relief_ncnn_files})
    list(APPEND storm_relief_copies COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${storm_relief_ncnn_models_DIR}/${file}" "${STORM_RELIEF_MODELS}/${file}")
endforeach()
foreach(path ${storm_upscale_copies})
    list(APPEND storm_relief_copies COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${path}" "${STORM_RELIEF_MODELS}/")
endforeach()
add_custom_target(storm_relief_model_files
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${STORM_RELIEF_MODELS}" ${storm_relief_copies}
    VERBATIM)
add_dependencies(storm_relief_model_files storm_relief_ncnn_models)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/ncnn.cmake")

add_library(storm_relief_core STATIC prepare.cpp relief.hpp tx_file.cpp tx_file.hpp backend.cpp backend.hpp upscale.cpp upscale.hpp)
target_compile_features(storm_relief_core PUBLIC cxx_std_20)
target_include_directories(storm_relief_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(storm_relief_core PUBLIC storm_bgfx_fx_core storm_texture_core storm_render_log PRIVATE ncnn)
set_target_properties(storm_relief_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(NOT ANDROID)
    # The device choice of a session (device_choice.hpp), with the backend replaced by stubs at link time.
    add_executable(storm_relief_device_choice_check tests/device_choice_check.cpp)
    target_link_libraries(storm_relief_device_choice_check PRIVATE storm_relief_core)
    # The finalization alone (finalizer.hpp) on synthetic planes: real encoders, writers and CPU admission, no models.
    add_executable(storm_relief_finalizer_check tests/finalizer_check.cpp)
    target_link_libraries(storm_relief_finalizer_check PRIVATE storm_relief_core)
    # The .tx reader (tx_file.hpp) on synthetic files: the levels it reads, and damaged files refused.
    add_executable(storm_relief_tx_check tests/tx_file_check.cpp)
    target_link_libraries(storm_relief_tx_check PRIVATE storm_relief_core)
    # The relief maps and the ×2 with the real models on a small synthetic game.
    add_executable(storm_relief_check tests/relief_check.cpp)
    target_link_libraries(storm_relief_check PRIVATE storm_relief_core)
    add_dependencies(storm_relief_check storm_relief_model_files)
    # The models' directory for the runner (not a compile definition: absolute paths there defeat ccache).
    file(WRITE "${CMAKE_BINARY_DIR}/relief_models_path.txt" "${STORM_RELIEF_MODELS}")
endif()

