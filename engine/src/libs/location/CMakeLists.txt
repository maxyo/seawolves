STORM_SETUP(
    TARGET_NAME location
    TYPE storm_module
    DEPENDENCIES animation collide core geometry model renderer sea sound_service util
)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/nlohmann_json.cmake")
target_link_libraries(location PRIVATE nlohmann_json::nlohmann_json)
