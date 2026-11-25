# The renderer's ASan tree (CMakeLists.txt here, STORM_CHECK_ASAN): configured on first use, then the TARGETS
# (comma-separated) built.
string(REPLACE "," ";" TARGETS "${TARGETS}")
if(NOT EXISTS "${BINARY}/CMakeCache.txt")
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${SOURCE}" -B "${BINARY}" -G Ninja -DSTORM_CHECK_ASAN=ON
                            "-DSTORM_TEST_SDL_INCLUDE=${SDL_INCLUDE}" "-DSTORM_TEST_SOURCES=${SOURCES}"
                            COMMAND_ERROR_IS_FATAL ANY)
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${BINARY}" --target ${TARGETS} COMMAND_ERROR_IS_FATAL ANY)
