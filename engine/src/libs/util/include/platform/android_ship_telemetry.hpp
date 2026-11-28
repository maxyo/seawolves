#pragma once
#include "../istring.hpp"
#include "android_gameplay.hpp"
#include "android_hud_presenter.hpp"
#include "storm_ui/hud_command.h"
#include "storm_ui/hud_state.h"
#include <cmath>
#include <array>
#include <cstdlib>
#include <cctype>
#include <limits>
#include <cstdio>
#include <tuple>

namespace storm::android
{
enum ShipHudMask : uint32_t
{
    ShipHull = 1,
    ShipCrew = 2,
    ShipAmmo = 4,
    ShipSailPosition = 8,
    ShipSailIntegrity = 16,
    ShipFleet = 32,
    ShipSpeed = 64,
    ShipPowder = 128,
    ShipDateTime = 256,
    ShipLocation = 512,
    ShipWind = 1024,
    ShipAll = 2047
};
inline std::optional<double> shipNumberValue(std::string_view text)
{
    if (text.empty() || text.size() > 128)
        return {};
    const std::string input(text);
    char *end = nullptr;
    const double value = std::strtod(input.c_str(), &end);
    if (end == input.c_str())
        return {};
    while (end < input.data() + input.size() && std::isspace(static_cast<unsigned char>(*end)))
        ++end;
    if (end != input.data() + input.size() || !std::isfinite(value) || value < 0)
        return {};
    return value;
}
// The date of the battle interface's "Date: …" caption without its prefix (the game's string "Date:"), or "".
inline std::string shipDateTimeText(std::string_view text, std::string_view prefix)
{
    if (prefix.empty() || !text.starts_with(prefix))
        return {};
    return std::string(text.substr(prefix.size()));
}
inline std::optional<double> shipDisplayedSpeed(double value)
{
    if (!std::isfinite(value))
        return {};
    // Match the original ExtPrint %.1f rounding, including signed reverse speed.
    char display[64];
    const int length = std::snprintf(display, sizeof(display), "%.1f", value);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(display))
        return {};
    char *end = nullptr;
    const auto rounded = std::strtod(display, &end);
    if (end != display + length || !std::isfinite(rounded))
        return {};
    return rounded == 0 ? 0 : rounded;
}
inline std::optional<int32_t> shipPowderCount(std::string_view text)
{
    const auto value = shipNumberValue(text);
    if (!value || *value > std::numeric_limits<int32_t>::max() || std::floor(*value) != *value)
        return {};
    return static_cast<int32_t>(*value);
}
struct ShipMetric
{
    double current, maximum;
    bool operator==(const ShipMetric &) const = default;
};
inline std::optional<ShipMetric> shipMetric(double current, double maximum)
{
    if (!std::isfinite(current) || !std::isfinite(maximum) || current < 0 || maximum <= 0)
        return {};
    return ShipMetric{current, maximum};
}
struct ShipSailGuard
{
    bool sea = false, up = false, down = false, mainCharacter = false;
    bool stopped = false, dead = false, canDropSails = true;
    std::optional<double> state;
};
inline bool shipSailControlAllowed(const ShipSailGuard &guard)
{
    return guard.sea && guard.up && guard.down && guard.mainCharacter && !guard.stopped && !guard.dead &&
           guard.canDropSails && guard.state && std::isfinite(*guard.state) && *guard.state >= 0 && *guard.state <= 1;
}
// The ship's picture as the original battle interface draws it: a cell of its ship-icon atlas
// (BattleInterface.ShipIcon / wm_sign.shiptexturename, battle_interface\ship_icons2.tga). `texture`
// is the texture-namespace name ("battle_interface/ship_icons2"), `uv` left, top, right, bottom 0…1.
struct ShipPicture
{
    std::string texture;
    std::array<float, 4> uv{};
    bool operator==(const ShipPicture &) const = default;
};
// "battle_interface\ship_icons2.tga.tx" → "battle_interface/ship_icons2"; "" when it is not a
// plain relative texture name (the UI resolves it under RESOURCE/Textures of the player's game).
inline std::string shipPictureTexture(std::string_view file)
{
    if (std::any_of(file.begin(), file.end(),
                    [](char c) { return static_cast<unsigned char>(c) < 0x20 || c == ':' || c == '"'; }))
        return {};
    std::string name = ResourceKey(std::string(file));
    for (const char *suffix : {".tx", ".tga"})
    {
        const std::string_view tail(suffix);
        if (name.size() > tail.size() && std::string_view(name).substr(name.size() - tail.size()) == tail)
            name.resize(name.size() - tail.size());
    }
    if (name.empty() || name.front() == '/' || name.find("..") != std::string::npos)
        return {};
    return name;
}
// A picture with a texture and a rectangle inside 0…1, or nothing.
inline std::optional<ShipPicture> shipPicture(std::string_view file, const std::array<float, 4> &uv)
{
    ShipPicture picture{shipPictureTexture(file), uv};
    for (const float v : uv)
        if (!std::isfinite(v) || v < 0.f || v > 1.f)
            return {};
    if (picture.texture.empty() || uv[2] <= uv[0] || uv[3] <= uv[1])
        return {};
    return picture;
}
inline storm::ui::hud::Picture hudPicture(const std::optional<ShipPicture> &picture)
{
    if (!picture)
        return {};
    return {picture->texture, picture->uv};
}
struct ShipTelemetryEntry
{
    int characterIndex = -1;
    std::string entityId, name, type;
    std::optional<ShipMetric> hull, crew, sails;
    std::optional<int32_t> shipClass;
    std::optional<ShipPicture> picture;
    bool operator==(const ShipTelemetryEntry &) const = default;
};
inline std::optional<int64_t> shipAmmoCount(std::string_view text)
{
    const auto value = shipNumberValue(text);
    if (!value || *value >= 0x1p63 || std::floor(*value) != *value)
        return {};
    return static_cast<int64_t>(*value);
}
struct ShipTelemetryFrame
{
    bool valid = false;
    ShipTelemetryEntry ship;
    std::vector<ShipTelemetryEntry> fleet;
    std::string ammo;
    int ammoType = -1;
    std::optional<double> ammoCount, sailState;
    // Cargo inventory per ammunition kind; an absent attribute is not an empty hold.
    std::array<std::optional<int64_t>, 4> ammoCounts{};
    bool canSetSails = false;
    bool captionsVisible = false;
    bool navigatorAvailable = false, navigatorZoomIn = false, navigatorZoomOut = false;
    uint64_t navigatorGeneration = 0, navigatorLayoutId = 0;
    // The battle interface's "not enough shot" flag; the scripts fold an empty powder hold into it
    // (AIShip.c Ship_NotEnoughBalls).
    std::optional<bool> navigatorAmmoWarning;

    std::optional<double> shipSpeedKnots, windSpeed;
    // How far the guns are loaded: the mean charge of the cannon sides the ship has, 0…1.
    std::optional<double> cannonCharge;
    // The world's time scale the scripts set (the game's acceleration, x1 = 1).
    std::optional<double> timeScale;
    std::vector<BattleAbility> abilities; // the ship abilities (android_ship_abilities.hpp)
    std::optional<int32_t> powderCount;
    std::string dateTimeText, locationText;
    bool operator==(const ShipTelemetryFrame &) const = default;
    uint32_t availableMask() const
    {
        if (!valid)
            return 0;
        uint32_t mask = (ship.hull ? ShipHull : 0) | (ship.crew ? ShipCrew : 0) | (ship.sails ? ShipSailIntegrity : 0) |
                        (sailState ? ShipSailPosition : 0);
        if (!ammo.empty() && ammoType >= 0 && ammoCount)
            mask |= ShipAmmo;
        if (std::all_of(fleet.begin(), fleet.end(), [](const auto &entry) {
                return entry.hull && entry.crew && entry.sails && !entry.name.empty();
            }))
            mask |= ShipFleet;
        if (captionsVisible)
        {
            if (shipSpeedKnots)
                mask |= ShipSpeed;
            if (windSpeed)
                mask |= ShipWind;
            if (powderCount)
                mask |= ShipPowder;
            if (!dateTimeText.empty())
                mask |= ShipDateTime;
            if (!locationText.empty())
                mask |= ShipLocation;
        }
        return mask;
    }
};
// Resolve current battle-interface captions on the game thread; implemented by battle_interface.
#ifdef STORM_TOUCH_UI
void readShipCaptions(ShipTelemetryFrame &frame, uint64_t mainShipId);
// The character's ship picture in the battle interface's ship icons (BIShipIcon: the picture
// number the script gives for BIDT_SHIPPICTURE, the icon atlas and its grid); implemented by
// battle_interface. Nothing while the battle interface has no such ship.
std::optional<ShipPicture> seaShipPicture(int characterIndex);
#endif

// Game-thread owned.
class ShipTelemetryState
{
    ShipTelemetryFrame frame_;
    uint64_t revision_ = 0, sequence_ = 0, hudRevision_ = 0;
    uint32_t lastPublishedTick_ = 0;
    bool published_ = false, forcePublish_ = true;
    std::tuple<uint64_t, uint64_t, uint64_t> publishedKey_; // revision, sequence and HUD revision published last
    struct SailTarget
    {
        uint64_t revision;
        int detent;
        uint64_t request; // the UI's number of the command
    };
    std::optional<SailTarget> target_;
    // The asked position will not be set.
    void dropTarget(const char *reason)
    {
        if (target_)
            hudAnswers.refused(target_->request, reason);
        target_.reset();
    }
    // current / maximum as a share (to the millionth of each, as the game prints them); negative = unknown.
    static double share(const std::optional<ShipMetric> &metric)
    {
        if (!metric || hudReading(metric->maximum) <= 0.0)
            return -1.0;
        return std::min(1.0, hudReading(metric->current) / hudReading(metric->maximum));
    }
    static storm::ui::hud::Ship::Vessel vessel(const ShipTelemetryEntry &entry)
    {
        return {entry.characterIndex,        entry.entityId,    entry.name,        entry.type,
                entry.shipClass.value_or(0), share(entry.hull), share(entry.crew), share(entry.sails),
                hudPicture(entry.picture)};
    }
    static double amount(const std::optional<double> &value)
    {
        return value && *value >= 0.0 ? hudReading(*value) : -1.0;
    }

  public:
    void update(uint64_t revision, ShipTelemetryFrame frame)
    {
        if (revision != revision_)
        {
            revision_ = revision;
            ++hudRevision_;
            dropTarget("a new gameplay revision");
            forcePublish_ = true;
        }
        if (frame_ != frame)
        {
            const auto sameFleetIdentity = [&]() {
                if (frame_.fleet.size() != frame.fleet.size())
                    return false;
                for (size_t i = 0; i < frame.fleet.size(); ++i)
                    if (frame_.fleet[i].characterIndex != frame.fleet[i].characterIndex ||
                        frame_.fleet[i].entityId != frame.fleet[i].entityId)
                        return false;
                return true;
            };
            // Another fleet, main ship, availability or caption visibility is a new HUD revision
            // for the UI to answer; ordinary changing HP values are not.
            const bool sameFleet = sameFleetIdentity();
            const bool sameMain = frame_.ship.entityId == frame.ship.entityId;
            if (!sameFleet || !sameMain || frame_.availableMask() != frame.availableMask() ||
                frame_.captionsVisible != frame.captionsVisible)
            {
                ++hudRevision_;
                forcePublish_ = true;
            }
            if (!sameMain)
                dropTarget("another main ship");
            if (frame_.valid != frame.valid)
                forcePublish_ = true;
            frame_ = std::move(frame);
            ++sequence_;
        }
        if (!frame_.valid)
            dropTarget("no ship");
    }
    const ShipTelemetryFrame &frame() const
    {
        return frame_;
    }
    uint64_t revision() const
    {
        return revision_;
    }
    uint64_t hudRevision() const
    {
        return hudRevision_;
    }
    // What the HUD's ship state counts its changes by (Ship::sequence): a sail command is answered with it.
    uint64_t sequence() const
    {
        return sequence_;
    }
    bool offerSails(const storm::ui::hud::Sails &sails)
    {
        if (sails.revision != revision_ || sails.position < 0 || sails.position > 2 || !frame_.valid ||
            !frame_.canSetSails)
            return false;
        dropTarget("a newer position was asked");
        target_ = SailTarget{revision_, sails.position, sails.request};
        return true;
    }
    struct TakenSails
    {
        int detent = 0;
        uint64_t request = 0;
    };
    // The sail position asked for this gameplay revision; a new revision or main ship drops it (update).
    std::optional<TakenSails> takeSails()
    {
        if (!target_)
            return {};
        if (!frame_.valid || !frame_.canSetSails)
        {
            dropTarget("the sails cannot be set now");
            return {};
        }
        const TakenSails taken{target_->detent, target_->request};
        target_.reset();
        return taken;
    }
    storm::ui::hud::Ship hud() const
    {
        storm::ui::hud::Ship out;
        out.gameplay_revision = revision_;
        out.sequence = sequence_;
        out.hud_revision = hudRevision_;
        if (!frame_.valid)
            return out;
        const auto &f = frame_;
        out.known = true;
        out.own = vessel(f.ship);
        for (const auto &entry : f.fleet)
            out.fleet.push_back(vessel(entry));
        out.captions = f.captionsVisible;
        out.navigator = f.navigatorAvailable;
        out.zoom_in = f.navigatorZoomIn;
        out.zoom_out = f.navigatorZoomOut;
        out.navigator_generation = f.navigatorGeneration;
        out.navigator_layout = f.navigatorLayoutId;
        out.speed = f.shipSpeedKnots ? std::abs(hudReading(*f.shipSpeedKnots)) : -1.0; // sternway is negative
        out.wind = amount(f.windSpeed);
        out.cannon_charge =
            f.cannonCharge && *f.cannonCharge >= 0.0 ? std::min(1.0, hudReading(*f.cannonCharge)) : -1.0;
        out.time_scale = amount(f.timeScale);
        out.abilities = hudAbilities(f.abilities);
        out.date_time = f.dateTimeText;
        out.location = f.locationText;
        out.ammo = f.ammo;
        out.ammo_count = amount(f.ammoCount);
        out.ammo_low = f.navigatorAmmoWarning.value_or(false);
        for (size_t i = 0; i < storm::ui::hud::Ship::kAmmo.size(); ++i)
            out.ammo_counts[i] =
                f.ammoCounts[i] && *f.ammoCounts[i] >= 0 ? static_cast<double>(*f.ammoCounts[i]) : -1.0;
        out.sail_state = amount(f.sailState) > 1.0 ? -1.0 : amount(f.sailState);
        out.can_set_sails = f.canSetSails;
        return out;
    }
    // The state to publish now: only when it changed since the last publication and the 100 ms throttle (bypassed
    // by a forced change) lets it go.
    std::optional<storm::ui::hud::Ship> publication(uint32_t tick)
    {
        if (published_ && publishedKey_ == std::tie(revision_, sequence_, hudRevision_))
            return {};
        if (published_ && !forcePublish_ && tick - lastPublishedTick_ < 100)
            return {};
        publishedKey_ = std::tie(revision_, sequence_, hudRevision_);
        lastPublishedTick_ = tick;
        published_ = true;
        forcePublish_ = false;
        return hud();
    }
};
inline ShipTelemetryState shipTelemetry;
} // namespace storm::android
