STORM_SETUP(
    TARGET_NAME util
    TYPE library
    TEST_DEPENDENCIES catch2
)# The platform headers publish the HUD's state structs to the UI host (storm_ui/hud_state.h), and on Android
# write the JSON of the channels of the UI's screens with nlohmann::json (the UI's).
target_include_directories(util PUBLIC "${CMAKE_SOURCE_DIR}/src/libs/ui/include")
if(ANDROID)
    target_link_libraries(util PUBLIC nlohmann_json::nlohmann_json)
endif()
