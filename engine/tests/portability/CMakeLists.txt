# x86 host oracle of the native SSE and the portable scalar math (STORM_FORCE_SCALAR_MATH) code paths.
foreach(variant sse scalar)
    set(defines "")
    if(variant STREQUAL scalar)
        set(defines STORM_FORCE_SCALAR_MATH=1)
    endif()
    storm_host_check(portability.${variant} SOURCES math.cpp INCLUDES ${glm_headers}
        DEFINES ${defines} OPTIONS -O2 -frounding-math -ffp-contract=off -fsanitize=undefined,float-cast-overflow
        -fno-sanitize-recover=all STD 17 CATCH2)
endforeach()
