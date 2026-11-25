# Android Frame Pacing (Swappy GL and Swappy Vk, one library) from the Android Game SDK (AGDK), Apache-2.0
# (third_party/licenses/Swappy-Apache-2.0.txt). The prebuilt static library of Google's
# androidx.games:games-frame-pacing AAR (Google Maven), pin swappy in engine/pins.json; the AAR is a
# zip that also carries the Java helper (SwappyDisplayManager) as a dex embedded in the static library,
# so the application needs no Java part. Built for c++_shared, API 21, like the engine's libraries.
# Offline: -DFETCHCONTENT_SOURCE_DIR_STORM_SWAPPY=/path/to/the/extracted/aar
# Linked by storm_bgfx (src/shim/frame_pacing.cpp) only: libengine.so has the one Swappy instance.
storm_pin(swappy version storm_swappy_version)
storm_fetch_pin(storm_swappy swappy DOWNLOAD_NAME "games-frame-pacing-${storm_swappy_version}.zip" STORM_POPULATE_ONLY)
FetchContent_MakeAvailable(storm_swappy)
set(storm_swappy_module "${storm_swappy_SOURCE_DIR}/prefab/modules/swappy_static")
set(storm_swappy_library "${storm_swappy_module}/libs/android.${ANDROID_ABI}/libswappy_static.a")
if(NOT EXISTS "${storm_swappy_library}")
    message(FATAL_ERROR "Swappy ${storm_swappy_version} has no static library for ${ANDROID_ABI}: ${storm_swappy_library}")
endif()
add_library(storm_swappy STATIC IMPORTED GLOBAL)
set_target_properties(storm_swappy PROPERTIES IMPORTED_LOCATION "${storm_swappy_library}")
target_link_libraries(storm_swappy INTERFACE android log EGL GLESv3)
set(storm_swappy_include "${storm_swappy_module}/include")
