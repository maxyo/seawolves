#pragma once
// What the activity's mailbox (the application's MobileMailbox) hands the engine thread once per frame: each message
// arrives through a typed native of GameActivity while the engine is inside GameActivity.deliverMobileActions.
#include "android_camera_look.hpp"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace storm::android::mailbox
{
struct AudioFocus
{
    bool audible; // false: another app owns the output
    bool ducked;  // another app plays a short sound over ours
};
struct GraphicsReload
{
};
struct HudOwnerLost
{
};
struct Back
{
};
struct LocationLoadCancel
{
    uint64_t generation;
};
struct CameraZoom
{
    float scale;
};
struct Shortcut
{
    std::string name;
};
using Message =
    std::variant<AudioFocus, GraphicsReload, HudOwnerLost, Back, LocationLoadCancel, CameraZoom, LookEvent, Shortcut>;

#ifdef __ANDROID__
// The engine thread's list while it is inside deliverMobileActions; null otherwise.
inline std::vector<Message> *delivering = nullptr;
#else
// The desktop's messages of the same kinds (its Back key, the HUD's shortcuts), on the game thread until the next
// iteration takes them (pollMobileActions).
inline std::vector<Message> pending;
#endif
} // namespace storm::android::mailbox
