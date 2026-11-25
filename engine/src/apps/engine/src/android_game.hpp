#pragma once
#ifdef __ANDROID__
// The Android stages of the engine's main loop: the touch UI's input and the activity's mailbox at the top of an
// iteration, the application lifecycle after the system events, the HUD's input before a game frame and the HUD's
// states after it. One instance for the loop; the input guards it installs refer to it.
#include "android_gameplay_state.hpp"
#include "platform/android_runtime.hpp"
#include "platform/android_shortcuts.hpp"
#include "platform/host_hooks.hpp"

#include <cstdint>
#include <optional>

namespace storm::engine
{
class AndroidGame
{
  public:
    // Installs the HUD input guards and tells every HUD channel that there is no gameplay yet.
    AndroidGame();
    AndroidGame(const AndroidGame &) = delete;
    AndroidGame &operator=(const AndroidGame &) = delete;

    // Top of an iteration: the UI host applies the touches, then the commands of the UI and the activity act.
    // Returns the input epoch the commands were taken in.
    uint64_t TakeInput();
    // After the system events were pumped: the lifecycle, and no gameplay where no game frame runs for the player.
    void AfterEvents(uint64_t inputEpoch);
    // HUD input that reaches the scripts before the frame, so that what it opens is drawn in this frame.
    void BeforeFrame();
    // The HUD's adapters after the frame: what the scripts changed reaches the UI in this frame.
    void AfterFrame();

    bool CanRun() const
    {
        return lifecycle_.canRun();
    }
    // A scene load's transaction starts: input made before it does not act after it.
    void CancelLoadingInput();
    // In the scene load's platform pump: the lifecycle; the state of the load when Android decides it.
    void ConsumeLoadingEvents();
    std::optional<platform::LoadingPlatformState> LoadingState(uint64_t generation, bool &cancelPending);
    // The engine is going down: the guards go, nothing of the game is usable, the UI host releases its textures.
    void Shutdown();

  private:
    const char *NotReady() const;
    bool Live() const;
    void Block(const char *blocked);

    android::Lifecycle lifecycle_;
    android::GameplayShortcuts shortcuts_;
    android::QuickGame quickGame_;
    android::GameplayState gameplay_;
    uint32_t lastCinematicCheck_ = 0;
};
} // namespace storm::engine
#endif
