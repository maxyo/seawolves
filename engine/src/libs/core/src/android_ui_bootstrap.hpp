#pragma once
#ifdef __ANDROID__
#include "platform/android_land_commands.hpp"
#include "platform/android_land_diagnostics.hpp"

#include <stdexcept>
#include <string>

namespace storm::android
{
// The script side of the portable UI: the app ships interface/android_ui_bootstrap.c, loaded once per script program
// after its Main or OnLoad returned, never inside a frame or an event handler. Without it the UI has no script side,
// so a missing or uncompiled bootstrap stops the start with the fatal dialog (the frame loop's ReportFatalError)
// instead of a game without its interface; it is not tried again until the program is replaced.
class UiBootstrap
{
    bool attempted_ = false;

  public:
    static constexpr const char *segment = "interface/android_ui_bootstrap.c";

    // The script tables and static events are going (a new program, a save loaded): the next load tries again. Only
    // the touch menu's taps for the old tables are revoked; missing presentation support must not close the stock menu.
    void reset()
    {
        attempted_ = false;
        landCommands.releaseUnavailableSource(LandCommandSource::Missing);
        traceLandAdapter(0, "script-reset");
    }
    // The interpreter's: isLoaded(segment), exists(path) in the file system, compile(segment).
    template <class IsLoaded, class Exists, class Compile>
    void load(const std::string &programDirectory, IsLoaded &&isLoaded, Exists &&exists, Compile &&compile)
    {
        if (attempted_)
            return;
        attempted_ = true;
        if (isLoaded(segment))
        {
            traceLandAdapter(0, "already-loaded");
            return;
        }
        const std::string path = programDirectory + segment;
        if (!exists(path.c_str()))
        {
            traceLandAdapter(0, "missing-entrypoint");
            throw std::runtime_error("The game's interface scripts are incomplete: " + path +
                                     " is missing. Reinstall the game.");
        }
        if (!compile(segment))
        {
            traceLandAdapter(0, "compile-failed");
            throw std::runtime_error("The game's interface scripts do not compile: " + path +
                                     " (see the compile log). Reinstall the game.");
        }
        traceLandAdapter(0, "loaded");
    }
};
} // namespace storm::android
#endif
