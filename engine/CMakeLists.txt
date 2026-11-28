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
else()
    include(host)
endif()

# The touch interface: the UI runtime drawn inside the engine frame (src/libs/ui_host) and the engine's touch glue
# (code under STORM_TOUCH_UI). Android's only interface; on the desktop a development build that runs the touch
# interface with the real game (README.md, "Linux"): the players' desktop build keeps the game's own interface.
option(STORM_TOUCH_UI "Build the touch interface in (Android; a development build on the desktop)" ${ANDROID})
if(ANDROID AND NOT STORM_TOUCH_UI)
    message(FATAL_ERROR "STORM_TOUCH_UI=OFF: Android has no other interface")
endif()
if(STORM_TOUCH_UI)
    set(STORM_ENABLE_UI ON)
    add_compile_definitions(STORM_TOUCH_UI=1)
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
