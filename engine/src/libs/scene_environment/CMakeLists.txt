# The scene environment (README.md): its per-frame state and its evaluation from a game's data.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/glm.cmake")
add_library(scene_environment STATIC src/evaluate.cpp src/legacy.cpp)
target_compile_features(scene_environment PUBLIC cxx_std_20)
target_include_directories(scene_environment PUBLIC include "${CMAKE_CURRENT_LIST_DIR}/../math/include")
target_link_libraries(scene_environment PUBLIC scene_data glm::glm-header-only)
set_target_properties(scene_environment PROPERTIES POSITION_INDEPENDENT_CODE ON)
