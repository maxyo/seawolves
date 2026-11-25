# The far LOD simplifier (src/libs/geometry/far_lod) on synthetic models, and the GEOM runtime integration: the real
# geometry translation units, their posted bake advanced by hand, recording host fixtures for the services.
set(CMAKE_CXX_FLAGS "-O3 -DNDEBUG")
add_subdirectory("${src}/libs/geometry/far_lod" library)
storm_host_check(far_lod OPTIONS ${warnings} LIBS storm_far_lod CATCH2)
storm_host_check(far_lod.runtime SOURCES runtime_check.cpp ${src}/libs/geometry/src/geom_far_lod.cpp
        ${src}/libs/geometry/src/geom_static.cpp ${src}/libs/geometry/src/collide.cpp
    INCLUDES ${glm_headers} ${src}/libs/geometry/src ${src}/libs/geometry/include ${src}/libs/core/include
             ${src}/libs/renderer_bgfx/include ${src}/libs/renderer_bgfx/fx/include
             ${src}/libs/renderer_bgfx/log/include ${fmt_headers}
    DEFINES FMT_HEADER_ONLY SPDLOG_FMT_EXTERNAL OPTIONS -Wall -Wextra -Wno-multichar
    LIBS storm_far_lod CATCH2)
