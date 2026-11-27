# The touch interface's scripts (the application's mobile-ui layer) on the production compiler and VM
# (tests/script_vm), with the engine's VM side of script screens (src/libs/ui_host/src/scripts) and the UI runtime's
# conversion of a view to props, over the fixture game in game/. ui_scripts.game measures the probe on
# $STORM_GAME_DIR (Not Run without one).
set(layer "${CMAKE_CURRENT_BINARY_DIR}/mobile-ui-layer")
storm_mobile_ui_layer(ui_scripts_layer "${layer}")
set(ui ${src}/libs/ui)
add_executable(ui_scripts_check check.cpp ${src}/libs/ui_host/src/scripts/script_segments.cpp
    ${src}/libs/ui_host/src/scripts/script_port.cpp ${ui}/src/interface_model.cpp ${ui}/src/component_spec.cpp
    ${ui}/src/screens/script_props.cpp ${ui}/src/screens/pictures.cpp)
add_dependencies(ui_scripts_check ui_scripts_layer)
target_include_directories(ui_scripts_check PRIVATE ${src}/libs/ui_host/src/scripts ${ui}/include ${ui}/src)
target_link_libraries(ui_scripts_check PRIVATE storm_script_vm storm_catch2_main nlohmann_json::nlohmann_json)
set(environment STORM_MOBILE_UI=${STORM_MOBILE_UI} STORM_UI_LAYER=${layer} STORM_UI_TESTS=${CMAKE_CURRENT_SOURCE_DIR}
    ASAN_OPTIONS=detect_leaks=0 XDG_DATA_HOME=${CMAKE_BINARY_DIR}/tmp/ui_scripts)
storm_check_test(ui_scripts COMMAND ui_scripts_check "~[game]" ENV ${environment}
    INPUTS ${STORM_MOBILE_UI}/PROGRAM ${STORM_MOBILE_UI}/design ${src}/libs/core)
storm_check_test(ui_scripts.game COMMAND ui_scripts_check "[game]" ENV ASAN_OPTIONS=detect_leaks=0
    XDG_DATA_HOME=${CMAKE_BINARY_DIR}/tmp/ui_scripts.game)
# The names of the interface's scripts against every script file of the game in $STORM_GAME_DIR (Not Run without one):
# the pack's build check with --game (design/build.py check_script_names).
storm_check_test(ui_scripts.names COMMAND sh -c
    "[ -z \"$STORM_GAME_DIR\" ] && exit 77 || exec \"$0\" \"$1\" --check --no-engine-check --game \"$STORM_GAME_DIR\""
    ${Python3_EXECUTABLE} ${STORM_MOBILE_UI}/design/build.py
    INPUTS ${STORM_MOBILE_UI}/PROGRAM ${STORM_MOBILE_UI}/design)
