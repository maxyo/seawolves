# The touch interface's scripts (app/mobile-ui) on the production compiler and VM (tests/script_language's
# storm_script_vm), with the engine's VM side of script screens (src/libs/ui_host/src/scripts) and the UI runtime's
# conversion of a view to props. ui_scripts.game measures the probe on $STORM_GAME_DIR (Not Run without one).
set(app_ui "${STORM_ROOT}/../app/mobile-ui")
set(header "${CMAKE_CURRENT_BINARY_DIR}/capabilities.h")
add_custom_command(OUTPUT "${header}"
    COMMAND "${Python3_EXECUTABLE}" "${app_ui}/design/build.py" --script-header "${header}"
    DEPENDS "${app_ui}/design/build.py" "${app_ui}/design/capabilities.json")
set(ui ${src}/libs/ui)
add_executable(ui_scripts_check check.cpp "${header}" ${src}/libs/ui_host/src/scripts/script_segments.cpp
    ${src}/libs/ui_host/src/scripts/script_port.cpp ${ui}/src/interface_model.cpp ${ui}/src/component_spec.cpp
    ${ui}/src/screens/script_props.cpp)
target_include_directories(ui_scripts_check PRIVATE ${src}/libs/ui_host/src/scripts ${ui}/include ${ui}/src)
target_link_libraries(ui_scripts_check PRIVATE storm_script_vm storm_catch2_main nlohmann_json::nlohmann_json)
set(environment STORM_UI_APP=${app_ui} STORM_UI_TESTS=${CMAKE_CURRENT_SOURCE_DIR} STORM_UI_HEADER=${header}
    ASAN_OPTIONS=detect_leaks=0 XDG_DATA_HOME=${CMAKE_BINARY_DIR}/tmp/ui_scripts)
storm_check_test(ui_scripts COMMAND ui_scripts_check "~[game]" ENV ${environment}
    INPUTS ${app_ui}/PROGRAM ${app_ui}/design/scripts.json ${app_ui}/design/screens ${src}/libs/core)
storm_check_test(ui_scripts.game COMMAND ui_scripts_check "[game]" ENV ASAN_OPTIONS=detect_leaks=0
    XDG_DATA_HOME=${CMAKE_BINARY_DIR}/tmp/ui_scripts.game)
