# Far LODs of .gm models (README.md): clusters and simplified levels by meshoptimizer. No engine dependencies but GLM:
# tests/far_lod builds it alone.
include(FetchContent)
# meshoptimizer (MIT, Arseny Kapoulkine; pin meshoptimizer). Licence: third_party/licenses/meshoptimizer-MIT.txt.
# Offline: -DFETCHCONTENT_SOURCE_DIR_STORM_MESHOPTIMIZER=/path/to/this/revision
storm_fetch_pin(storm_meshoptimizer meshoptimizer STORM_POPULATE_ONLY)
FetchContent_MakeAvailable(storm_meshoptimizer)

# Only the parts we call.
set(meshopt "${storm_meshoptimizer_SOURCE_DIR}/src")
add_library(storm_meshoptimizer STATIC
    "${meshopt}/allocator.cpp"
    "${meshopt}/indexcodec.cpp"
    "${meshopt}/simplifier.cpp"
    "${meshopt}/spatialorder.cpp"
    "${meshopt}/vcacheoptimizer.cpp")
target_include_directories(storm_meshoptimizer SYSTEM PUBLIC "${meshopt}")
set_target_properties(storm_meshoptimizer PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(storm_far_lod STATIC far_lod.cpp far_lod.hpp layout.cpp)
target_compile_features(storm_far_lod PUBLIC cxx_std_20)
target_include_directories(storm_far_lod PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../util/include") # fnv_hash.hpp
target_link_libraries(storm_far_lod PRIVATE storm_meshoptimizer)
# GLM through the engine's storm_glm.h (src/libs/math/README.md).
include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/glm.cmake")
target_include_directories(storm_far_lod PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../../math/include")
target_link_libraries(storm_far_lod PUBLIC glm::glm-header-only)
set_target_properties(storm_far_lod PROPERTIES POSITION_INDEPENDENT_CODE ON)
