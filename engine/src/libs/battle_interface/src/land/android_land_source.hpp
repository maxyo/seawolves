#pragma once
#ifdef __ANDROID__
#include "attributes.h"
#include "platform/android_land_commands.hpp"
#include "platform/android_land_diagnostics.hpp"
#include "platform/android_script_errors.hpp"

#include <charconv>
#include <optional>
#include <string_view>

namespace storm::android
{
// The land menu's source: askScripts runs the scripts' AndroidLandRefresh, which writes root.AndroidLand. Ready, or
// Forbidden by pendingAllowed, only when that event ran without a script error and wrote a complete, valid version 1;
// otherwise Missing, and no AndroidLand is left for a later refresh to inherit a target or completion marker from.
template <class AskScripts> LandCommandSource refreshLandSource(ATTRIBUTES *root, AskScripts &&askScripts)
{
    const auto clear = [&] {
        if (auto *meta = root ? root->GetAttributeClass("AndroidLand") : nullptr)
            root->DeleteAttributeClassX(meta);
    };
    clear();
    const auto errorsBefore = scriptErrorEpoch;
    askScripts();
    const auto missing = [&](const char *reason) {
        clear();
        traceLandAdapter(3, reason);
        return LandCommandSource::Missing;
    };
    if (scriptErrorEpoch != errorsBefore)
        return missing("script-error");
    auto *meta = root ? root->GetAttributeClass("AndroidLand") : nullptr;
    const auto field = [&](const char *name) -> std::optional<std::string_view> {
        auto *value = meta ? meta->GetAttributeClass(name) : nullptr;
        if (!value || !value->HasValue())
            return {};
        return value->GetValue();
    };
    if (field("version") != "1")
        return missing("missing-fresh-version");
    const auto index = [&](const char *name) {
        auto value = field(name);
        int32_t parsed = -1;
        if (!value || value->empty())
            return false;
        const auto result = std::from_chars(value->data(), value->data() + value->size(), parsed);
        return result.ec == std::errc() && result.ptr == value->data() + value->size() && parsed >= -1;
    };
    const auto pending = field("pendingAllowed"), reload = field("reload"), hereLabel = field("hereLabel");
    if (!index("dialog"))
        return missing("invalid-dialog-target");
    if (!index("officer"))
        return missing("invalid-officer-target");
    if (!index("body"))
        return missing("invalid-body-target");
    if (pending != "0" && pending != "1")
        return missing("invalid-pending-allowed");
    if (!reload || !hudTextValid(*reload))
        return missing("invalid-reload-target");
    // The place of the town's fast-travel table the hero stands in (-1: none): the travel stage marks its tile.
    if (!index("herePicture") || !index("hereTexture") ||
        (field("herePicture") == "-1") != (field("hereTexture") == "-1"))
        return missing("invalid-here-place");
    if (!hereLabel || !hudTextValid(*hereLabel))
        return missing("invalid-here-label");
    const bool allowed = pending == "1";
    traceLandAdapter(3, allowed ? "fresh-version-1-allowed" : "fresh-version-1-forbidden");
    return allowed ? LandCommandSource::Ready : LandCommandSource::Forbidden;
}
} // namespace storm::android
#endif
