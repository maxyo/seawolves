cmake_minimum_required(VERSION 3.18)
project(storm-engine)
include("${CMAKE_CURRENT_LIST_DIR}/cmake/StormBuildCache.cmake")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
if(NOT ANDROID)
    message(STATUS "Linux host build of the engine (cmake/host.cmake). Android: "
        "-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/android-arm64.cmake (cmake/android.cmake, tools/android/README.md)")
endif()
include(StormSetup)

option(STORM_ENABLE_SAFE_MODE "Enable additional runtime checks" OFF)

### Set up output paths
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Release)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Debug)

if(ANDROID)
    include(android)
    # The game's portable UI is drawn inside the engine frame (src/libs/ui_host): the UI
    # runtime and its bgfx backend are part of libengine.so.
    set(STORM_ENABLE_UI ON)
else()
    include(host)
endif()

### Set up build scripts
set(SRC_DIRS "src" CACHE STRING "File locations for public header files" FORCE)
set(PUBLIC_INCLUDE_DIRS "include" CACHE STRING "File locations for source files" FORCE)
set(TESTSUITE_DIRS "testsuite" CACHE STRING "File locations for tests" FORCE)
set(RESOURCE_DIRS "rsrc" CACHE STRING "File locations for tests" FORCE)

set(SOURCE_FILE_EXTENSIONS "*.cpp" CACHE STRING "File extensions for source files" FORCE)
set(HEADER_FILE_EXTENSIONS "*.h" "*.hpp" CACHE STRING "File extensions for header files" FORCE)
###

include(build_stamp)
include(StormAppContract)
storm_app_contract_headers()
add_subdirectory(src)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT engine)
