# Dependencies of the Android engine build: the SDL prefix and the NDK are passed explicitly
# (-DSTORM_ANDROID_DEPS, -DSTORM_ANDROID_NDK; tools/android/ci_engine.sh does it), the pinned sources are fetched
# (cmake/engine_dependencies.cmake).
if(NOT IS_DIRECTORY "${STORM_ANDROID_DEPS}")
    message(FATAL_ERROR "Set STORM_ANDROID_DEPS to the prepared dependency prefix (tools/android/ci_dependencies.sh)")
endif()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
add_compile_options(-fsigned-char)
add_library(sdl SHARED IMPORTED GLOBAL)
# Only SDL's headers from the prefix: a prefix may still hold fmt 8 headers
# in <prefix>/include/fmt, which must never shadow the pinned fmt.
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/sdl-include")
file(CREATE_LINK "${STORM_ANDROID_DEPS}/include/SDL2" "${CMAKE_BINARY_DIR}/sdl-include/SDL2" SYMBOLIC)
set_target_properties(sdl PROPERTIES IMPORTED_LOCATION "${STORM_ANDROID_DEPS}/lib/libSDL2.so"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/sdl-include;${STORM_ANDROID_DEPS}/include/SDL2")
set(SDL2_LIBRARIES sdl)
# No mimalloc on Android: emulated TLS (below API 29) makes its first allocation call libc malloc, so
# interposing malloc re-enters the uninitialized heap, and shared libc++ frees storage internally,
# so an engine-only new override corrupts allocator ownership. C and C++ allocation stay on Bionic.
# Release = the RelWithDebInfo flags (src/CMakeLists.txt) plus hidden visibility, section GC and ThinLTO for the
# engine's own targets. Only SDL_main (SDLMAIN_DECLSPEC) and the JNI entry points (JNIEXPORT) stay exported; the debug
# info stays for the release packaging to split off.
option(STORM_ANDROID_RELEASE_LTO "ThinLTO for Android Release builds" ON)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections)
    add_link_options(-Wl,--gc-sections)
    if(STORM_ANDROID_RELEASE_LTO)
        add_compile_options(-flto=thin)
        add_link_options(-flto=thin)
        # lld's ThinLTO cache: a relink code-generates only the modules that changed; lld prunes it itself.
        add_link_options("-Wl,--thinlto-cache-dir=${STORM_DEPS_CACHE}/thinlto-cache"
                         "-Wl,--thinlto-cache-policy=prune_after=168h:cache_size_bytes=8g")
    endif()
endif()
# fmt is linked statically into the engine (built here with the flags above, ThinLTO included).
include(engine_dependencies)
# Catch2 writes its output to the Android log (__android_log_write) on Android.
target_link_libraries(catch2 INTERFACE log)
# NDK exposes the Android platform zlib ABI. Do not package a competing libz.so.
add_library(zlib INTERFACE)
target_link_libraries(zlib INTERFACE z)
# Short ABI name of the toolchain file (cmake/toolchains/android-<arch>.cmake): arm64 for every
# release, x86_64 for emulator debug builds.
if(ANDROID_ABI STREQUAL "arm64-v8a")
    set(STORM_ANDROID_ARCH arm64)
elseif(ANDROID_ABI STREQUAL "x86_64")
    set(STORM_ANDROID_ARCH x86_64)
else()
    message(FATAL_ERROR "Unsupported ANDROID_ABI ${ANDROID_ABI} (arm64-v8a or x86_64)")
endif()
include(ffmpeg)
storm_ffmpeg(android-${STORM_ANDROID_ARCH})
string(TOUPPER "${STORM_ANDROID_ARCH}" storm_watermark_arch)
set(STORM_BUILD_PLATFORM "Android_${storm_watermark_arch}") # the watermark's platform (cmake/build_stamp.cmake)
