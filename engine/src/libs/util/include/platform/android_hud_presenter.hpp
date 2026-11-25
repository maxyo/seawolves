#pragma once
#include <array>
#include <string>
#include <string_view>
#include <utility>
#if defined(__ANDROID__) && __has_include(<android/log.h>)
#include <android/log.h>
#define STORM_HUD_PRESENTER_LOG 1
#endif

namespace storm::android
{
// Touch HUD declarations carry protocol ownership only. Android never restores legacy
// screen-space HUD drawing, including before mount and after a runtime failure.
// World-space ship markers remain part of the scene.
struct HudPresenters
{
    bool land = false, sea = false, worldMap = false, spyglass = false;
    bool failed = false;
    bool declare(std::string_view hud)
    {
        bool *owner = hud == "land"       ? &land
                      : hud == "sea"      ? &sea
                      : hud == "worldmap" ? &worldMap
                      : hud == "spyglass" ? &spyglass
                                          : nullptr;
        if (!owner)
            return false;
        *owner = true;
        failed = false;
        return true;
    }
    bool any() const
    {
        return land || sea || worldMap || spyglass;
    }
    std::string lost()
    {
        if (failed)
            return {};
        failed = true;
        return "HUD presenter lost: the UI runtime failed; touch HUD recovery required; legacy HUD remains disabled";
    }
};
inline HudPresenters hudPresenters;

enum class LegacyLayerKind
{
    Hud,   // screen-space HUD: always refused on Android
    World, // drawn in the scene: shown under the touch HUD too
};
struct LegacyLayer
{
    std::string_view name;
    LegacyLayerKind kind;
};
inline constexpr std::array<LegacyLayer, 12> kLegacyLayers{{
    {"land interface", LegacyLayerKind::Hud},        // location images and texts: the touch HUD's location line
    {"land command list", LegacyLayerKind::Hud},     // stock action list: the touch HUD's sheet
    {"log and actions", LegacyLayerKind::Hud},       // log lines and action hints
    {"active perk icons", LegacyLayerKind::Hud},     // perk icons over the time control
    {"timer bar", LegacyLayerKind::Hud},             // countdown bar
    {"interface cursor", LegacyLayerKind::Hud},      // mouse pointer of the original interface
    {"loading spinner", LegacyLayerKind::Hud},       // loading/saving spinner
    {"world map instruments", LegacyLayerKind::Hud}, // compass, date, coordinates, flag: the touch HUD's instruments
    {"world map fleet", LegacyLayerKind::Hud},       // ship signs of the squadron: the touch HUD's fleet
    {"ship bars", LegacyLayerKind::World},           // hull/sails/crew bars and relation icon over ships and forts
    {"ship pointer", LegacyLayerKind::World},        // pointer over an order's target ship
    // Not world-space, but the touch HUD has nothing in its place yet: shown until it is ported
    // into the HUD (two side-strength bars), then refused again.
    {"boarding status", LegacyLayerKind::World}, // crew strength bars of a deck fight
}};
// A layer's name, checked against kLegacyLayers when the caller is compiled: a layer nobody classified does not build.
struct LegacyLayerName
{
    size_t index = 0;
    consteval LegacyLayerName(const char *name)
    {
        while (kLegacyLayers[index].name != name)
            if (++index == kLegacyLayers.size())
                throw "legacy layer not classified in kLegacyLayers";
    }
};

// A legacy layer asks to draw. Android refuses every screen-space HUD layer (the touch HUD is
// the only HUD) and logs the first refusal of each. A world-space layer is never refused.
inline bool legacyDrawRefused(LegacyLayerName name)
{
    const LegacyLayer &layer = kLegacyLayers[name.index];
#ifdef STORM_HUD_PRESENTER_LOG
    static std::array<bool, kLegacyLayers.size()> reported{};
    if (!std::exchange(reported[name.index], true))
    {
        if (layer.kind == LegacyLayerKind::World)
            __android_log_print(ANDROID_LOG_INFO, "StormHud", "world-space layer '%.*s' shown under the touch HUD",
                                static_cast<int>(layer.name.size()), layer.name.data());
        else
            __android_log_print(ANDROID_LOG_WARN, "StormHud",
                                "legacy HUD layer '%.*s' refused: Android uses the touch HUD",
                                static_cast<int>(layer.name.size()), layer.name.data());
    }
#endif
    return layer.kind == LegacyLayerKind::Hud;
}
} // namespace storm::android
