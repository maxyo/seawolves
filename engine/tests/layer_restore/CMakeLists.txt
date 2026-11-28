# The production file service over a layer the system trims while the game runs (tests/script_vm's library).
add_executable(layer_restore_check check.cpp)
target_link_libraries(layer_restore_check PRIVATE storm_script_vm storm_catch2_main)
storm_check_test(layer_restore COMMAND layer_restore_check ENV ASAN_OPTIONS=detect_leaks=0
    INPUTS ${src}/libs/core ${CMAKE_CURRENT_SOURCE_DIR})
