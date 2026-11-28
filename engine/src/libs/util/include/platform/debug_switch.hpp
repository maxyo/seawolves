#pragma once
// The touch interface's development traces and checks, switched on by name: on Android a log tag
// (adb shell setprop log.tag.<name> DEBUG, before the game starts), on the desktop an environment variable
// (<name>=DEBUG). Their lines go to logcat under that tag, on the desktop to the engine's log.
#include <string>
#include <string_view>

#ifdef __ANDROID__
#include <android/log.h>
#include <sys/system_properties.h>
#else
#include <cstdlib>
#include <spdlog/spdlog.h>
#endif

namespace storm::platform
{
inline bool debugSwitch(const char *name)
{
#ifdef __ANDROID__
    char value[PROP_VALUE_MAX] = {};
    __system_property_get((std::string("log.tag.") + name).c_str(), value);
#else
    const char *set = std::getenv(name);
    const std::string_view value = set ? set : "";
#endif
    return std::string_view(value) == "DEBUG" || std::string_view(value) == "VERBOSE";
}

inline void debugTrace(const char *name, const std::string &line)
{
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_DEBUG, name, "%s", line.c_str());
#else
    spdlog::info("[{}] {}", name, line);
#endif
}
} // namespace storm::platform
