// What both Android hosts of the UI (android/jni_host.cpp and the engine's ui_host) read from their natives.
#pragma once

#include "platform/jni_text.hpp"
#include "storm_ui/ui.h"

#include <android/log.h>
#include <jni.h>
#include <sys/system_properties.h>
#include <time.h>

#include <optional>
#include <string>
#include <string_view>

namespace storm::ui::jni
{

// CLOCK_MONOTONIC, the clock of MotionEvent times (SystemClock.uptimeMillis), in seconds.
inline double MonotonicSeconds()
{
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<double>(now.tv_sec) + static_cast<double>(now.tv_nsec) * 1e-9;
}

// adb shell setprop log.tag.<tag> DEBUG (or VERBOSE).
inline bool Loggable(const char *tag)
{
    char value[PROP_VALUE_MAX] = {};
    __system_property_get((std::string("log.tag.") + tag).c_str(), value);
    return std::string_view(value) == "DEBUG" || std::string_view(value) == "VERBOSE";
}

// A text argument of a native as UTF-8; nothing (logged) when it cannot be read: the call then does nothing.
inline std::optional<std::string> Text(JNIEnv *env, jstring value, const char *what)
{
    std::string error;
    auto text = storm::jni_text::TryToUtf8(env, value, error);
    if (!text)
        __android_log_print(ANDROID_LOG_ERROR, "StormUi", "%s from Java is not read: %s", what, error.c_str());
    return text;
}

inline std::string Bytes(JNIEnv *env, jbyteArray value)
{
    const jsize size = env->GetArrayLength(value);
    std::string text(static_cast<size_t>(size), '\0');
    env->GetByteArrayRegion(value, 0, size, reinterpret_cast<jbyte *>(text.data()));
    return text;
}

// The phase of a touch native (0 down, 1 move, 2 up, 3 cancel); nothing (logged) for another value.
inline std::optional<TouchPhase> Phase(jint phase)
{
    if (phase >= 0 && phase <= 3)
        return static_cast<TouchPhase>(phase);
    __android_log_print(ANDROID_LOG_ERROR, "StormUi", "touch with phase %d dropped", static_cast<int>(phase));
    return std::nullopt;
}

} // namespace storm::ui::jni
