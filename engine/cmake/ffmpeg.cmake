# FFmpeg (LGPL-2.1-or-later, shared libraries) for in-game video (src/libs/video).
# tools/ffmpeg/build.py configures and builds the pinned source (pin "ffmpeg" in engine/pins.json) once per source
# revision, flag set and compiler, at configure time, into a cache shared by all build directories of the machine.
# Flags and licence notes: tools/ffmpeg/README.md.
#   STORM_FFMPEG_CACHE   build cache, default ${STORM_DEPS_CACHE}/ffmpeg-cache
function(storm_ffmpeg target)
    storm_fetch_pin(storm_ffmpeg ffmpeg STORM_POPULATE_ONLY)
    FetchContent_MakeAvailable(storm_ffmpeg)
    storm_pin(ffmpeg commit revision)
    set(STORM_FFMPEG_CACHE "${STORM_DEPS_CACHE}/ffmpeg-cache" CACHE PATH "FFmpeg build cache")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(command "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/ffmpeg/build.py" --source "${storm_ffmpeg_SOURCE_DIR}"
        --revision ${revision} --cache "${STORM_FFMPEG_CACHE}" --target ${target})
    if(target MATCHES "^android-")
        list(APPEND command --ndk "${STORM_ANDROID_NDK}")
    endif()
    execute_process(COMMAND ${command} RESULT_VARIABLE result OUTPUT_VARIABLE prefix OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "FFmpeg build failed (tools/ffmpeg/build.py)")
    endif()
    message(STATUS "FFmpeg: ${prefix}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/ffmpeg/build.py")
    add_library(storm_ffmpeg INTERFACE)
    target_include_directories(storm_ffmpeg SYSTEM INTERFACE "${prefix}/include")
    # Packaged next to libengine.so (the application copies runtime/ffmpeg/*.so into the APK).
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/runtime/ffmpeg")
    foreach(name avformat avcodec swscale swresample avutil)
        add_library(storm_ffmpeg_${name} SHARED IMPORTED GLOBAL)
        set_target_properties(storm_ffmpeg_${name} PROPERTIES IMPORTED_LOCATION "${prefix}/lib/lib${name}.so")
        target_link_libraries(storm_ffmpeg INTERFACE storm_ffmpeg_${name})
        if(target MATCHES "^android-")
            file(COPY "${prefix}/lib/lib${name}.so" DESTINATION "${CMAKE_BINARY_DIR}/runtime/ffmpeg")
        endif()
    endforeach()
    if(target MATCHES "^android-")
        file(COPY "${prefix}/configure.txt" DESTINATION "${CMAKE_BINARY_DIR}/runtime/ffmpeg")
    endif()
endfunction()
