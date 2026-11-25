# xxHash (BSD-2-Clause, header-only): the 64-bit content hashes of the renderer's caches (texture core, fx::Hasher, the
# ×2 files). storm_xxhash: its include directory, every function inline (XXH_INLINE_ALL), so nothing is compiled
# separately. Pinned in engine/pins.json; requires storm_fetch_pin (cmake/StormBuildCache.cmake).
# Offline: -DFETCHCONTENT_SOURCE_DIR_STORM_XXHASH.
if(NOT TARGET storm_xxhash)
    include(FetchContent)
    storm_fetch_pin(storm_xxhash xxhash STORM_POPULATE_ONLY)
    FetchContent_MakeAvailable(storm_xxhash)
    add_library(storm_xxhash INTERFACE)
    target_include_directories(storm_xxhash SYSTEM INTERFACE "${storm_xxhash_SOURCE_DIR}")
    target_compile_definitions(storm_xxhash INTERFACE XXH_INLINE_ALL)
endif()
