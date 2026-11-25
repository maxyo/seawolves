# The math contracts: storm::math helpers and the GLM layouts the engine relies on, under ASan/UBSan, built as the
# engine builds src/libs/math (StormSetup) and optimised like a release build.
set(CMAKE_CXX_FLAGS "-O3 -DNDEBUG")
include("${STORM_ROOT}/cmake/StormSetup.cmake")
set(SRC_DIRS src)
set(PUBLIC_INCLUDE_DIRS include)
set(TESTSUITE_DIRS testsuite)
set(RESOURCE_DIRS rsrc)
set(SOURCE_FILE_EXTENSIONS "*.cpp")
set(HEADER_FILE_EXTENSIONS "*.h" "*.hpp")
add_subdirectory("${src}/libs/math" math)
foreach(contract check packed_vector_include builders_check helpers_check)
    # -fno-strict-aliasing mirrors the engine build (src/CMakeLists.txt).
    storm_host_check(glm_math.${contract} SOURCES ${contract}.cpp INCLUDES sdk_shape LIBS math
        OPTIONS -Wall -Wextra -ffp-contract=off -fno-strict-aliasing ${asan_strict} CATCH2)
endforeach()
