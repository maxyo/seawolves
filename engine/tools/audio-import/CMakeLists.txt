# storm-audio-import, a host tool: FMOD Studio banks (FSB5 Vorbis) and WAV files into Ogg Vorbis (README.md), and its
# library audio_import. The engine does not link either.
#   cmake -S tools/audio-import -B build/audio-import -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/audio-import
# The host checks build both inside their project (tests/CMakeLists.txt).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.28)
    project(storm_audio_import LANGUAGES C CXX)
    include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/StormBuildCache.cmake")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/cmake/xiph.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/nlohmann_json.cmake")
find_package(ZLIB REQUIRED)

add_library(audio_import STATIC
    src/bank.cpp
    src/fsb5.cpp
    src/string_table.cpp
    src/vorbis_remux.cpp
    src/vorbis_setup.cpp
    src/wav_encode.cpp)
target_include_directories(audio_import PUBLIC include)
target_compile_features(audio_import PUBLIC cxx_std_20)
target_link_libraries(audio_import PUBLIC storm_vorbis storm_ogg PRIVATE ZLIB::ZLIB)

add_executable(storm-audio-import main.cpp)
target_link_libraries(storm-audio-import PRIVATE audio_import nlohmann_json::nlohmann_json)
if(NOT MSVC)
    target_compile_options(audio_import PRIVATE -Wall -Wextra)
    target_compile_options(storm-audio-import PRIVATE -Wall -Wextra)
endif()
