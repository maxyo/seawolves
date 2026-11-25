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

// The engine thread's list while it is inside deliverMobileActions; null otherwise.
inline std::vector<Message> *delivering = nullptr;
} // namespace storm::android::mailbox
