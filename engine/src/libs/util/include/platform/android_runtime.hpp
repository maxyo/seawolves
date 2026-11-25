#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <SDL2/SDL.h>
#ifdef __ANDROID__
#include <SDL2/SDL_system.h>
#include <android/log.h>
#include <jni.h>
#include <optional>
#include <string>
#include "app_upcalls.hpp"
#include "jni_text.hpp"
#endif

namespace storm::android
{
// How often the app went to the background (Lifecycle::consume), for code that must notice it without seeing the
// event queue (the automatic graphics preset's measurement, renderer auto_preset.hpp). Game thread.
inline std::atomic<uint32_t> backgroundEntries{0};

// Only mutate on the game thread while consuming SDL's event queue.
class Lifecycle
{
  public:
    void consume(unsigned type)
    {
        switch (type)
        {
        case SDL_APP_WILLENTERBACKGROUND:
        case SDL_APP_DIDENTERBACKGROUND:
            if (!background_)
                backgroundEntries.fetch_add(1);
            background_ = true;
            break;
        case SDL_APP_DIDENTERFOREGROUND:
            background_ = false;
            break;
        case SDL_APP_TERMINATING:
        case SDL_QUIT:
            terminating_ = true;
            break;
        default:
            break;
        }
    }
    bool canRun() const
    {
        return !background_ && !terminating_;
    }
    bool terminating() const
    {
        return terminating_;
    }

  private:
    bool background_ = false;
    bool terminating_ = false;
};

struct StoragePaths
{
    std::filesystem::path game;
    std::filesystem::path user;
};

// The player's private data (saves, options, logs): known before any game folder is, so the logs open before the
// game folder is checked and its failure is logged.
inline std::filesystem::path userDirectory(const char *internal)
{
    if (!internal || !*internal)
        throw std::runtime_error("Android internal storage is unavailable");
    const std::filesystem::path privateRoot(internal);
    if (!privateRoot.is_absolute())
        throw std::runtime_error("Android storage roots must be absolute");
    return privateRoot / "storm";
}

// The launcher may select an existing shared-storage folder; user data stays private.
inline StoragePaths storagePaths(const char *internal, const char *external,
                                 const std::filesystem::path &selectedGame = {})
{
    const auto user = userDirectory(internal);
    const std::filesystem::path dataRoot(external && *external ? external : internal);
    if (!dataRoot.is_absolute())
        throw std::runtime_error("Android storage roots must be absolute");
    if (!selectedGame.empty() && !selectedGame.is_absolute())
        throw std::runtime_error("Selected game directory must be absolute");
    return {selectedGame.empty() ? dataRoot / "game" : selectedGame, user};
}

#ifdef __ANDROID__
inline std::filesystem::path userDirectory()
{
    return userDirectory(SDL_AndroidGetInternalStoragePath());
}

// An upcall to the game activity's method `java` (upcall::GameActivity): `call(env, activity, method)` makes it and
// returns whether it had what it needed. The activity ships with this engine, so a missing method is logged as an
// error; a Java exception is described to logcat and cleared. True when the call ran, succeeded and Java threw
// nothing; false without a game activity.
template <class Call> bool CallActivity(const JavaMethod &java, Call &&call)
{
    auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    const auto activity = env ? static_cast<jobject>(SDL_AndroidGetActivity()) : nullptr;
    if (!activity)
        return false;
    const auto type = env->GetObjectClass(activity);
    const auto method = type ? env->GetMethodID(type, java.name, java.signature) : nullptr;
    if (type)
        env->DeleteLocalRef(type);
    bool done = false;
    if (method)
        done = call(env, activity, method) && !env->ExceptionCheck();
    else
    {
        env->ExceptionClear();
        __android_log_print(ANDROID_LOG_ERROR, "StormEngine", "GameActivity.%s%s is missing", java.name,
                            java.signature);
    }
    if (env->ExceptionCheck())
    {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    env->DeleteLocalRef(activity);
    return done;
}

// The text a String getter of the activity returns, as CallActivity's call: `take` throws `unavailable` when there is
// none, `unreadable` and why when it is not valid text.
struct ActivityText
{
    std::optional<std::string> text;
    std::string error;
    bool operator()(JNIEnv *env, jobject activity, jmethodID method)
    {
        const auto value = static_cast<jstring>(env->CallObjectMethod(activity, method));
        if (env->ExceptionCheck() || !value)
        {
            if (value)
                env->DeleteLocalRef(value);
            return false;
        }
        text = jni_text::TryToUtf8(env, value, error);
        env->DeleteLocalRef(value);
        return true;
    }
    std::string take(bool called, const char *unavailable, const char *unreadable)
    {
        if (!error.empty())
            throw std::runtime_error(unreadable + error);
        if (!called || !text)
            throw std::runtime_error(unavailable);
        return *text;
    }
};

inline StoragePaths storagePaths()
{
    ActivityText read;
    const bool called = CallActivity(upcall::GameActivity::getGameDataDirectory, read);
    const auto path = read.take(called, "Game folder unavailable. Check file access in the launcher or import a ZIP.",
                                "Cannot read the selected game directory: ");
    if (path.empty())
        throw std::runtime_error("No game directory selected");
    return storagePaths(SDL_AndroidGetInternalStoragePath(), SDL_AndroidGetExternalStoragePath(),
                        std::filesystem::u8path(path));
}

// The app's one cache root (GameActivity.getCacheRoot: (getExternalCacheDir() ?: getCacheDir())/engine): everything
// the engine derives and can make again. «Очистить кэш» and low storage delete it at any time, even while the game
// runs, so each subsystem below it rebuilds what is missing and logs it. Use fs::GetCachePath().
inline std::filesystem::path cacheDirectory()
{
    ActivityText read;
    const bool called = CallActivity(upcall::GameActivity::getCacheRoot, read);
    const auto result =
        std::filesystem::u8path(read.take(called, "Android cache root (GameActivity.getCacheRoot) is unavailable",
                                          "Android cache root (GameActivity.getCacheRoot) cannot be read: "));
    if (!result.is_absolute())
        throw std::runtime_error("Android cache root (GameActivity.getCacheRoot) is not an absolute path");
    return result;
}

inline void prepareGameDirectory()
{
    const auto paths = storagePaths();

    if (!std::filesystem::is_regular_file(paths.game / "engine.ini"))
        throw std::runtime_error("Place your legally obtained game files, including engine.ini, in " +
                                 paths.game.string());
    std::filesystem::current_path(paths.game);
}
#endif
} // namespace storm::android
