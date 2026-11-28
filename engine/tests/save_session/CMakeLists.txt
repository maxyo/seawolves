# Saving and loading through the production core and VM (tests/script_vm) with a program of the check's own: what the
# save folder holds at every step of a save.
add_executable(save_session_check check.cpp)
target_link_libraries(save_session_check PRIVATE storm_script_vm storm_catch2_main)
storm_check_test(save_session COMMAND save_session_check
    ENV ASAN_OPTIONS=detect_leaks=0 XDG_DATA_HOME=${CMAKE_BINARY_DIR}/tmp/save_session
    INPUTS ${src}/libs/core ${CMAKE_CURRENT_SOURCE_DIR})
