# The pinned sources of the torrent library (tools/android/build_torrent.py configures this project first): each
# release fetched once per machine into the shared read-only copy of storm_fetch_declare (cmake/StormBuildCache.cmake),
# the upstream fix in ../patches applied in this build directory's own view of libtorrent. Writes sources.json: the
# source directory of each dependency, for build_torrent.py. The pins: engine/pins.json.
cmake_minimum_required(VERSION 3.24)
project(storm_torrent_sources NONE)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/StormBuildCache.cmake")
set(patch "${CMAKE_CURRENT_LIST_DIR}/../patches/libtorrent-android-ca-roots.patch")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${patch}")
storm_fetch_pin(storm_libtorrent libtorrent STORM_POPULATE_ONLY STORM_PATCHES . "${patch}")
# libtorrent's git submodule deps/try_signal, which a release tarball leaves empty.
storm_fetch_pin(storm_try_signal try_signal STORM_POPULATE_ONLY)
# The same pin as the archive reader's (android/archive/sources): one shared copy.
storm_fetch_pin(storm_boost boost STORM_POPULATE_ONLY)
storm_fetch_pin(storm_openssl openssl STORM_POPULATE_ONLY)
set(names libtorrent try_signal boost openssl)
list(TRANSFORM names PREPEND storm_ OUTPUT_VARIABLE targets)
FetchContent_MakeAvailable(${targets})
# The patched view is this build directory's own: the submodule goes in as a link to its shared copy.
set(submodule "${storm_libtorrent_SOURCE_DIR}/deps/try_signal")
set(linked "")
if(IS_SYMLINK "${submodule}")
    file(READ_SYMLINK "${submodule}" linked)
endif()
if(NOT linked STREQUAL storm_try_signal_SOURCE_DIR)
    file(REMOVE "${submodule}")
    file(MAKE_DIRECTORY "${storm_libtorrent_SOURCE_DIR}/deps")
    file(CREATE_LINK "${storm_try_signal_SOURCE_DIR}" "${submodule}" SYMBOLIC)
endif()
set(json "{}")
foreach(name ${names})
    string(JSON json SET "${json}" "${name}" "\"${storm_${name}_SOURCE_DIR}\"")
endforeach()
file(WRITE "${CMAKE_BINARY_DIR}/sources.json" "${json}\n")
