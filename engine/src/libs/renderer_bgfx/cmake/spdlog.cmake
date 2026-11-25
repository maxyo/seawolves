# spdlog for the renderer's log (log/include/storm/render/log.hpp). Inside the engine the engine's own target is used
# (cmake/engine_dependencies.cmake: the pinned spdlog with the pinned fmt). A stand-alone build of this directory
# (the UI host project, tests/renderer_bgfx) fetches the same pin (spdlog in engine/pins.json) and uses the fmt it
# bundles, header-only. Requires storm_fetch_pin (cmake/StormBuildCache.cmake).
# Offline: -DFETCHCONTENT_SOURCE_DIR_STORM_SPDLOG.
if(NOT TARGET spdlog)
    include(FetchContent)
    storm_fetch_pin(storm_spdlog spdlog STORM_POPULATE_ONLY)
    FetchContent_MakeAvailable(storm_spdlog)
    add_library(spdlog INTERFACE)
    target_include_directories(spdlog SYSTEM INTERFACE "${storm_spdlog_SOURCE_DIR}/include")
endif()
