# Shared body of the Storm Android toolchain files (android-arm64.cmake, android-x86_64.cmake);
# each sets STORM_ANDROID_TOOLCHAIN_ABI and includes this file.
# Configure with -DSTORM_ANDROID_NDK=/absolute/path/to/android-ndk-<pinned version> (pin "ndk" in engine/pins.json).
# This file never downloads or installs a toolchain.
if(NOT IS_ABSOLUTE "${STORM_ANDROID_NDK}" OR
   NOT EXISTS "${STORM_ANDROID_NDK}/build/cmake/android.toolchain.cmake")
    message(FATAL_ERROR "Set STORM_ANDROID_NDK to an installed Android NDK absolute path; no SDK/NDK is downloaded automatically.")
endif()
# Another NDK compiles different code (its libc++ and clang): only the pinned release is accepted.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../../pins.json" storm_pins)
string(JSON storm_ndk_pin GET "${storm_pins}" ndk version)
file(STRINGS "${STORM_ANDROID_NDK}/source.properties" storm_ndk_release REGEX "^Pkg\\.ReleaseName *=")
string(REGEX REPLACE "^Pkg\\.ReleaseName *= *" "" storm_ndk_release "${storm_ndk_release}")
if(NOT storm_ndk_release STREQUAL storm_ndk_pin)
    message(FATAL_ERROR "STORM_ANDROID_NDK is NDK '${storm_ndk_release}', not the pinned ${storm_ndk_pin} (engine/pins.json)")
endif()
if(DEFINED ANDROID_ABI AND NOT ANDROID_ABI STREQUAL STORM_ANDROID_TOOLCHAIN_ABI)
    message(FATAL_ERROR "This Storm Android toolchain file requires ANDROID_ABI=${STORM_ANDROID_TOOLCHAIN_ABI}; "
                        "use cmake/toolchains/android-<arch>.cmake of the wanted ABI")
endif()
set(ANDROID_ABI ${STORM_ANDROID_TOOLCHAIN_ABI} CACHE STRING "Android ABI")
set(ANDROID_PLATFORM android-26 CACHE STRING "Android minimum API")
if(NOT ANDROID_PLATFORM MATCHES "^android-([0-9]+)$")
    message(FATAL_ERROR "ANDROID_PLATFORM must have the form android-26 (or a higher API)")
endif()
if(CMAKE_MATCH_1 LESS 26)
    message(FATAL_ERROR "Storm Android baseline requires API 26 or newer (ANDROID_PLATFORM=${ANDROID_PLATFORM}; a build directory configured for an older API must be deleted)")
endif()
if(DEFINED ANDROID_STL AND NOT ANDROID_STL STREQUAL "c++_shared")
    message(FATAL_ERROR "Storm Android uses c++_shared consistently across native libraries")
endif()
set(ANDROID_STL c++_shared CACHE STRING "Android C++ runtime")
include("${STORM_ANDROID_NDK}/build/cmake/android.toolchain.cmake")
# The NDK assigns this list itself; append wrapper inputs after including it.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES STORM_ANDROID_NDK ANDROID_ABI ANDROID_PLATFORM ANDROID_STL)
