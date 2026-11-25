include_guard(GLOBAL)
include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# GLM (pin "glm" in engine/pins.json). Offline builds can set FETCHCONTENT_SOURCE_DIR_STORM_GLM to a pre-extracted
# copy of this revision.
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "Header-only GLM" FORCE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "Do not build upstream GLM tests" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "Do not install bundled GLM" FORCE)
storm_fetch_pin(storm_glm glm)
FetchContent_MakeAvailable(storm_glm)
