# Caribbean Legend's foliage files and the trees a density draws (README.md). No engine dependencies but GLM,
# nlohmann/json and zlib: tests/foliage builds it alone.
add_library(storm_foliage STATIC foliage_files.cpp foliage_files.hpp selection.cpp selection.hpp)
target_compile_features(storm_foliage PUBLIC cxx_std_20)
target_include_directories(storm_foliage PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../util/include"
                                                "${CMAKE_CURRENT_SOURCE_DIR}/../math/include")
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/glm.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/nlohmann_json.cmake")
# The platform's zlib on Android and on the host alike (cmake/android.cmake).
target_link_libraries(storm_foliage PUBLIC glm::glm-header-only z PRIVATE nlohmann_json::nlohmann_json)
set_target_properties(storm_foliage PROPERTIES POSITION_INDEPENDENT_CODE ON)
