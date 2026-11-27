# libogg and libvorbis (pins libogg, libvorbis in engine/pins.json; BSD-3-Clause). Their own CMake files predate CMake
# 3.5, so the targets below compile the upstream sources directly: storm_ogg, storm_vorbis (with vorbisenc, without
# vorbisfile).
storm_fetch_pin(storm_libogg libogg STORM_POPULATE_ONLY)
storm_fetch_pin(storm_libvorbis libvorbis STORM_POPULATE_ONLY)
FetchContent_MakeAvailable(storm_libogg storm_libvorbis)

set(storm_ogg_generated "${CMAKE_CURRENT_BINARY_DIR}/storm_ogg_include")
set(INCLUDE_INTTYPES_H 0)
set(INCLUDE_STDINT_H 1)
set(INCLUDE_SYS_TYPES_H 0)
set(SIZE16 int16_t)
set(USIZE16 uint16_t)
set(SIZE32 int32_t)
set(USIZE32 uint32_t)
set(SIZE64 int64_t)
set(USIZE64 uint64_t)
configure_file("${storm_libogg_SOURCE_DIR}/include/ogg/config_types.h.in" "${storm_ogg_generated}/ogg/config_types.h"
    @ONLY)
add_library(storm_ogg STATIC "${storm_libogg_SOURCE_DIR}/src/bitwise.c" "${storm_libogg_SOURCE_DIR}/src/framing.c")
target_include_directories(storm_ogg SYSTEM PUBLIC "${storm_ogg_generated}" "${storm_libogg_SOURCE_DIR}/include")

set(storm_vorbis_lib "${storm_libvorbis_SOURCE_DIR}/lib")
set(storm_vorbis_sources mdct smallft block envelope window lsp lpc analysis synthesis psy info floor1 floor0 res0
    mapping0 registry codebook sharedbook lookup bitrate vorbisenc)
list(TRANSFORM storm_vorbis_sources PREPEND "${storm_vorbis_lib}/")
list(TRANSFORM storm_vorbis_sources APPEND ".c")
add_library(storm_vorbis STATIC ${storm_vorbis_sources})
target_include_directories(storm_vorbis SYSTEM PUBLIC "${storm_libvorbis_SOURCE_DIR}/include" PRIVATE "${storm_vorbis_lib}")
target_link_libraries(storm_vorbis PUBLIC storm_ogg)
if(NOT WIN32)
    target_link_libraries(storm_vorbis PUBLIC m)
endif()
# Third-party code at its pin: its warnings are not ours to fix.
if(NOT MSVC)
    target_compile_options(storm_ogg PRIVATE -w)
    target_compile_options(storm_vorbis PRIVATE -w)
endif()
