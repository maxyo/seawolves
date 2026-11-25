# cmake -DSTORM_SDL_SOURCE=<dir> -P tools/android/sdl_source.cmake (from <dir>'s parent directory)
# <dir> becomes a link to the pinned SDL (pin "sdl" of engine/pins.json) with patches/sdl2-android-ndk30.patch: the
# patched view (storm_fetch_declare STORM_PATCHES) of the shared copy, under _deps of the working directory. The
# Android SDL build compiles it; the application takes SDL's Java classes and licences from it.
cmake_minimum_required(VERSION 3.28)
if(NOT STORM_SDL_SOURCE)
    message(FATAL_ERROR "usage: cmake -DSTORM_SDL_SOURCE=<dir> -P ${CMAKE_CURRENT_LIST_FILE}")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/StormBuildCache.cmake")
# One writer of the view at a time.
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/_deps")
file(LOCK "${CMAKE_BINARY_DIR}/_deps/storm_sdl.lock" GUARD PROCESS)
storm_fetch_pin(storm_sdl sdl STORM_PATCHES . "${CMAKE_CURRENT_LIST_DIR}/patches/sdl2-android-ndk30.patch")
if(IS_SYMLINK "${STORM_SDL_SOURCE}")
    file(READ_SYMLINK "${STORM_SDL_SOURCE}" linked)
    if(linked STREQUAL FETCHCONTENT_SOURCE_DIR_STORM_SDL)
        return()
    endif()
    file(REMOVE "${STORM_SDL_SOURCE}")
elseif(EXISTS "${STORM_SDL_SOURCE}")
    message(FATAL_ERROR "${STORM_SDL_SOURCE} is not a link to the patched SDL (an SDL checkout of an earlier build "
        "layout?): remove it")
endif()
file(CREATE_LINK "${FETCHCONTENT_SOURCE_DIR_STORM_SDL}" "${STORM_SDL_SOURCE}" SYMBOLIC)
