# The Effects entity and the EFFECTS_LIBRARY natives (README.md); effects play through storm/render/effekseer.hpp.
STORM_SETUP(
    TARGET_NAME effects
    TYPE storm_module
    DEPENDENCIES core math renderer storm_bgfx util
)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/nlohmann_json.cmake")
target_link_libraries(effects PRIVATE nlohmann_json::nlohmann_json)
