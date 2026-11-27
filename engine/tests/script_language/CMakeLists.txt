# The script language cases (cases/*.c) on the production compiler and VM: script_check (src/apps/script_check) built
# from core's sources, without the engine's other modules (the cases need only core's built-ins and script_check's own
# test library): the VM library of tests/script_vm.
include("${STORM_TESTS_DIR}/script_vm/script_vm.cmake")
# AddressSanitizer: the VM's memory errors fail the checks. The script cache (--cache) is keyed on the binary's build ID.
storm_script_vm(storm_script_vm OPTIONS -O1 -g -fsanitize=address LINK_OPTIONS -fsanitize=address "-Wl,--build-id=sha1")
add_executable(script_language_check ${src}/apps/script_check/src/main.cpp)
target_link_libraries(script_language_check PRIVATE storm_script_vm)

# Each case compiled, and loaded from a script cache another compiler wrote (README of src/apps/script_check). The
# compiler's logs go below the check's temporary directory.
file(GLOB cases CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cases/*.c)
foreach(check script_language script_language.cache)
    set(mode test)
    if(check STREQUAL script_language.cache)
        list(APPEND mode --cache)
    endif()
    storm_check_test(${check} COMMAND script_language_check ${mode} ${cases}
        ENV ASAN_OPTIONS=detect_leaks=0 XDG_DATA_HOME=${CMAKE_BINARY_DIR}/tmp/${check}
        INPUTS ${src}/libs/core ${src}/apps/script_check)
endforeach()
