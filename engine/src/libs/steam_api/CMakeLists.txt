STORM_SETUP(
    TARGET_NAME steam_api
    TYPE storm_module
    DEPENDENCIES core
)

if(NOT ANDROID)
    # The player's settings (platform/desktop_settings.hpp).
    include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/nlohmann_json.cmake")
    target_link_libraries(steam_api PRIVATE nlohmann_json::nlohmann_json)
endif()
