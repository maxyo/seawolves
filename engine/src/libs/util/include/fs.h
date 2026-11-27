#pragma once

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include "platform/storage.hpp"
#ifdef __ANDROID__
#include "platform/android_runtime.hpp"
#else
#include "platform/desktop_folders.hpp"
#endif

/* Filesystem proxy */
namespace fs
{
using namespace std::filesystem;

// Everything derived and rebuildable (texture, program and script caches, baked maps, relief maps): Android's app
// cache (storm::android::cacheDirectory), which the system may empty at any time; the game's cache folder on desktop.
// Saves, options and logs never go here.
inline path GetCachePath()
{
#if defined(__ANDROID__)
    static const path path = storm::android::cacheDirectory();
    return path;
#else
    const auto &cache = storm::storage::folders().cache;
    if (cache.empty())
        throw std::logic_error("This program keeps no cache");
    return cache;
#endif
}

// Known before the game folder, so that a failure to find the game is logged.
inline path GetLogsPath()
{
#if defined(__ANDROID__)
    static const path path = storm::android::userDirectory() / "Logs";
#else
    static const path path = storm::desktop::logsFolder(
        storm::desktop::baseFolders([](const char *name) -> const char * { return std::getenv(name); }));
#endif
    return path;
}

inline path GetSaveDataPath()
{
    return storm::storage::folders().saves;
}

constexpr char ENGINE_INI_FILE_NAME[] = "engine.ini";
} // namespace fs
