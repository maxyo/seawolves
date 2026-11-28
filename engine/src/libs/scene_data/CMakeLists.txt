# The scene data parsers (include/storm/scene_data/scene_data.hpp), over nlohmann/json.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/nlohmann_json.cmake")
add_library(scene_data STATIC src/json_reader.cpp src/model_material.cpp src/lighting_preset.cpp src/tone_mapping.cpp
    src/time_of_day.cpp src/atmosphere.cpp src/texture_library.cpp src/mesh_lod_profiles.cpp)
target_compile_features(scene_data PUBLIC cxx_std_20)
target_include_directories(scene_data PUBLIC include PRIVATE src)
target_link_libraries(scene_data PRIVATE nlohmann_json::nlohmann_json)
set_target_properties(scene_data PROPERTIES POSITION_INDEPENDENT_CODE ON)
