# The engine's pinned dependencies of both builds (Android, Linux host), fetched with storm_fetch_pin
# (engine/pins.json): fmt (compiled into the engine), the headers of spdlog, fast_float and Catch2, miniaudio with the
# stb_vorbis it bundles (patched: tools/android/patches) for the audio library, and FFmpeg (cmake/ffmpeg.cmake). The
# licence texts the APK ships of them are written to <build>/engine-licenses (the application's build_apk.py).
get_filename_component(storm_miniaudio_patch "${CMAKE_CURRENT_LIST_DIR}/../tools/android/patches/miniaudio-0.11.23-error-cleanup.patch"
    ABSOLUTE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${storm_miniaudio_patch}")
storm_fetch_pin(storm_fmt fmt STORM_POPULATE_ONLY)
storm_fetch_pin(storm_spdlog spdlog STORM_POPULATE_ONLY)
storm_fetch_pin(storm_fast_float fast_float STORM_POPULATE_ONLY)
storm_fetch_pin(storm_catch2 catch2 STORM_POPULATE_ONLY)
storm_fetch_pin(storm_miniaudio miniaudio STORM_POPULATE_ONLY STORM_PATCHES . "${storm_miniaudio_patch}")
FetchContent_MakeAvailable(storm_fmt storm_spdlog storm_fast_float storm_catch2 storm_miniaudio)

set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${storm_fmt_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/fmt" EXCLUDE_FROM_ALL)
add_library(fast_float INTERFACE)
target_include_directories(fast_float INTERFACE "${storm_fast_float_SOURCE_DIR}/include")
add_library(spdlog INTERFACE)
target_include_directories(spdlog INTERFACE "${storm_spdlog_SOURCE_DIR}/include")
target_compile_definitions(spdlog INTERFACE SPDLOG_FMT_EXTERNAL)
target_link_libraries(spdlog INTERFACE fmt)
add_library(catch2 INTERFACE)
target_include_directories(catch2 INTERFACE "${storm_catch2_SOURCE_DIR}/single_include")
add_library(storm_open_audio STATIC "${CMAKE_SOURCE_DIR}/android/audio/implementation.cpp"
    "${CMAKE_SOURCE_DIR}/android/audio/service_backend.cpp")
target_include_directories(storm_open_audio PUBLIC "${CMAKE_SOURCE_DIR}/android/audio" "${storm_miniaudio_SOURCE_DIR}"
    "${storm_miniaudio_SOURCE_DIR}/extras")
target_link_libraries(storm_open_audio PUBLIC dl m)

set(storm_licenses "${CMAKE_BINARY_DIR}/engine-licenses")
file(MAKE_DIRECTORY "${storm_licenses}")
foreach(pair fmt:LICENSE spdlog:LICENSE fast_float:LICENSE-MIT fast_float:LICENSE-APACHE)
    string(REPLACE ":" ";" pair "${pair}")
    list(GET pair 0 project)
    list(GET pair 1 file)
    configure_file("${storm_${project}_SOURCE_DIR}/${file}" "${storm_licenses}/${project}-${file}" COPYONLY)
endforeach()
# The single-file libraries' licence is the comment block that ends the source.
foreach(file miniaudio.h extras/stb_vorbis.c)
    file(READ "${storm_miniaudio_SOURCE_DIR}/${file}" text)
    string(FIND "${text}" "This software is available" at REVERSE)
    string(SUBSTRING "${text}" 0 ${at} head)
    string(FIND "${head}" "/*" start REVERSE)
    if(at EQUAL -1 OR start EQUAL -1)
        message(FATAL_ERROR "No licence block at the end of ${storm_miniaudio_SOURCE_DIR}/${file}")
    endif()
    string(SUBSTRING "${text}" ${start} -1 notice)
    get_filename_component(name "${file}" NAME_WE)
    file(WRITE "${storm_licenses}/${name}.txt" "${notice}")
endforeach()
configure_file("${storm_miniaudio_patch}" "${storm_licenses}/miniaudio-storm-patch.txt" COPYONLY)
