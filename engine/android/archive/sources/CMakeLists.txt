# The pinned sources of the archive reader (android/archive/build.py configures this project first): each release
# fetched once per machine into the shared read-only copy of storm_fetch_declare (cmake/StormBuildCache.cmake), the
# upstream fixes in ../patches applied in this build directory's own view of it. Writes sources.json: the source
# directory of each dependency, for build.py. The pins: engine/pins.json.
cmake_minimum_required(VERSION 3.24)
project(storm_archive_sources NONE)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/StormBuildCache.cmake")
set(patches "${CMAKE_CURRENT_LIST_DIR}/../patches")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${patches}/innoextract-1.9-bounds.patch" "${patches}/libarchive-3.8.10-no-exit.patch"
    "${patches}/libarchive-3.8.10-undecryptable-zip.patch")
storm_fetch_pin(storm_innoextract innoextract STORM_POPULATE_ONLY STORM_PATCHES . "${patches}/innoextract-1.9-bounds.patch")
storm_fetch_pin(storm_libarchive libarchive STORM_POPULATE_ONLY
    STORM_PATCHES . "${patches}/libarchive-3.8.10-no-exit.patch" "${patches}/libarchive-3.8.10-undecryptable-zip.patch")
storm_fetch_pin(storm_xz xz STORM_POPULATE_ONLY)
storm_fetch_pin(storm_bzip2 bzip2 STORM_POPULATE_ONLY)
# The same pin as the torrent library's (android/torrent/sources): one shared copy.
storm_fetch_pin(storm_boost boost STORM_POPULATE_ONLY)
storm_fetch_pin(storm_zlib zlib STORM_POPULATE_ONLY)
set(names innoextract libarchive xz bzip2 boost zlib)
list(TRANSFORM names PREPEND storm_ OUTPUT_VARIABLE targets)
FetchContent_MakeAvailable(${targets})
set(json "{}")
foreach(name ${names})
    string(JSON json SET "${json}" "${name}" "\"${storm_${name}_SOURCE_DIR}\"")
endforeach()
file(WRITE "${CMAKE_BINARY_DIR}/sources.json" "${json}\n")
