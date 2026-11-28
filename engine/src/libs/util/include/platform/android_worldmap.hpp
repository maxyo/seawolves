#pragma once
#include "android_ship_telemetry.hpp"
#include "storm_ui/hud_command.h"
#include "storm_ui/hud_state.h"
#include "android_hud_presenter.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>

namespace storm::android
{
// Same zone flags produced by WdmPlayerShip::TestInStorm; absent data is not fair weather.
inline std::optional<int> mapStormState(const char *inStorm, const char *tornado)
{
    if (!inStorm || !tornado)
        return std::nullopt;
    const std::string_view storm(inStorm), funnel(tornado);
    if ((storm != "0" && storm != "1") || (funnel != "0" && funnel != "1"))
        return std::nullopt;
    if (storm == "0")
        return funnel == "0" ? std::optional<int>(0) : std::nullopt;
    return funnel == "1" ? 2 : 1;
}
// The moon the game shows on that day of the month (WhrSun.c: texture Moon00…Moon07 = day / 4):
// 0 new (black), 1…3 waxing, 4 full, 5…7 waning. A copy of the game's day ranges: the game picks the
// moon only while it builds a sea or land weather, and the map builds none for its own day.
inline std::optional<int> mapMoonPhase(int day)
{
    if (day < 1 || day > 31)
        return std::nullopt;
    return day / 4;
}
// The hours night ends and begins in the game's hourly weathers (the Night flag of each hour's record, as
// FindWeatherByHour picks it): {dawn, dusk}; nullopt unless every hour has a record and night ends and begins once.
inline std::optional<std::pair<int, int>> mapDayHours(const std::array<std::optional<bool>, 24> &night)
{
    std::optional<int> dawn, dusk;
    for (int h = 0; h < 24; ++h)
    {
        const auto &now = night[h], &before = night[(h + 23) % 24];
        if (!now || !before)
            return std::nullopt;
        if (*before == *now)
            continue;
        auto &edge = *now ? dusk : dawn;
        if (edge)
            return std::nullopt;
        edge = h;
    }
    if (!dawn || !dusk)
        return std::nullopt;
    return std::pair{*dawn, *dusk};
}
enum WorldMapCoverage : uint32_t
{
    MapInstruments = 1,
    MapShips = 2,
    MapCommands = 4
};
struct MapInstrumentFrame
{
    uint64_t owner = 0;
    bool visible = false, complete = false;
    std::string date, coordinates;
    std::optional<double> hour, windForce, windAngle, heading, morale;
    std::optional<double> speed;     // the map ship's speed, share of its maximum 0…1
    std::optional<double> timeScale; // the world's time scale the scripts set (x1 = 1)
    std::optional<int> foodDays, rumDays, flag, stormState;
    std::optional<int> moonPhase;                // mapMoonPhase of the map's day
    std::optional<std::pair<int, int>> dayHours; // mapDayHours of the game's weathers
};
struct MapShipFrame
{
    std::string id, name;
    std::optional<double> hull, sails;
    std::optional<int> crew, shipClass, crewMax;
    std::string type;                   // Optional stable RealShips model name; never the vessel's display name.
    std::optional<ShipPicture> picture; // the sign's ship picture (wm_sign shiptexturename, faceuv)
};
struct MapCommand
{
    std::string id, label;
    bool operator==(const MapCommand &) const = default;
};
struct WorldMapFrame
{
    bool available = false, instrumentsVisible = false, shipsVisible = false, shipsComplete = true,
         commandsReady = false;
    uint64_t owner = 0, gameplayRevision = 0;
    std::string identity, context;
    MapInstrumentFrame instruments;
    std::vector<MapShipFrame> ships;
    std::vector<MapCommand> commands;
    uint32_t availableMask() const
    {
        uint32_t mask = 0;
        const auto &i = instruments;
        if (instrumentsVisible && i.complete && !i.date.empty() && i.hour && i.windForce && i.windAngle && i.heading &&
            i.morale && i.foodDays)
            mask |= MapInstruments;
        if (shipsVisible && shipsComplete && !ships.empty() &&
            std::all_of(ships.begin(), ships.end(), [](const auto &s) {
                return !s.id.empty() && !s.name.empty() && s.hull && s.sails && s.crew && s.shipClass;
            }))
            mask |= MapShips;
        if (commandsReady && !commands.empty() && std::all_of(commands.begin(), commands.end(), [](const auto &c) {
                return !c.id.empty() && !c.label.empty();
            }))
            mask |= MapCommands;
        return mask;
    }
};
// The world-map commands that leave the map for the open sea (WmInterface.c: "Выйти в море"):
// the touch HUD's way to stop on purpose, which on PC is WMapCancel (Space). Attacking or
// approaching a ship or storm stays the game's own command.
inline bool leavesForOpenSea(std::string_view command)
{
    return command == "EnterToSea" || command == "EnterToIsland";
}
class WorldMapHud
{
  public:
    struct Owner
    {
        uint64_t revision = 0, activity = 0, gameplay = 0, world = 0;
        bool operator==(const Owner &) const = default;
    };

  private:
    std::optional<std::array<uint32_t, 6>> labelCompass_;
    Owner labelCompassOwner_;
    WorldMapFrame frame_;
    uint64_t revision_ = 0, activity_ = 0;
    std::string pending_;
    uint64_t pendingRequest_ = 0; // the UI's number of the pending command
    // The pending command will not run.
    void drop(const char *reason)
    {
        hudAnswers.refused(pendingRequest_, reason);
        pending_.clear();
        pendingRequest_ = 0;
    }
    // What the HUD moves every frame, at the resolution it draws: the sail lever's knob is the speed
    // share (already in hundredths), the compass turns with the course and the wind arrow with the
    // wind in whole degrees, the wind force reads in hundredths, and the clock's minute moves the
    // sun or moon over the compass (the map's time runs at worldMap.date.hourPerSec, 1.5 game hours
    // a second: about one minute a frame).
    using Live = std::tuple<uint64_t, uint64_t, std::optional<double>, std::optional<long>, std::optional<long>,
                            std::optional<long>, std::optional<long>>;
    Live live() const
    {
        const auto &i = frame_.instruments;
        const auto round = [](const std::optional<double> &value, double scale) {
            return value ? std::optional<long>(std::lround(*value * scale)) : std::nullopt;
        };
        constexpr double degrees = 180.0 / 3.14159265358979323846;
        return {revision_,
                activity_,
                i.speed,
                round(i.heading, degrees),
                round(i.windAngle, degrees),
                round(i.windForce, 100.0),
                i.hour ? std::optional<long>(std::lround(std::floor(*i.hour * 60.0))) : std::nullopt};
    }
    std::optional<Live> publishedLive_;
    uint32_t publishedAt_ = 0;

  public:
    // When the state goes to the UI (publishWorldMapHud): at once when a revision or a reading the
    // HUD moves changes (live()), so that the lever, the compass, the wind, the clock and the sun or
    // moon follow the map frame by frame; otherwise every 200 ms, for the readings that change only
    // with the map's events (morale, provisions, the fleet).
    bool due(uint32_t tick) const
    {
        return !publishedLive_ || *publishedLive_ != live() || tick - publishedAt_ >= 200;
    }
    void published(uint32_t tick)
    {
        publishedLive_ = live();
        publishedAt_ = tick;
    }
    Owner owner() const
    {
        return {revision_, activity_, frame_.gameplayRevision, frame_.owner};
    }
    MapInstrumentFrame source;
    uint64_t contextActionOwner = 0;
    std::string contextAction;
    void update(WorldMapFrame frame)
    {
        if (frame.available != frame_.available || frame.owner != frame_.owner || frame.identity != frame_.identity ||
            frame.context != frame_.context || frame.commands != frame_.commands ||
            frame.commandsReady != frame_.commandsReady || frame.instruments.flag != frame_.instruments.flag ||
            frame.instruments.coordinates != frame_.instruments.coordinates ||
            frame.instruments.rumDays.has_value() != frame_.instruments.rumDays.has_value() ||
            frame.gameplayRevision != frame_.gameplayRevision || frame.availableMask() != frame_.availableMask())
        {
            ++revision_;
            drop("the map changed before the command ran");
        }
        frame_ = std::move(frame);
        if (!frame_.available)
            drop("the map is gone");
    }
    // A new UI host: what the old one drew or tapped does not count.
    void resetActivity()
    {
        ++activity_;
        ++revision_;
        drop("a new UI host");
    } // revision_ >= 1 once available
    const WorldMapFrame &frame() const
    {
        return frame_;
    }
    uint64_t revision() const
    {
        return revision_;
    }
    // Last drawn UI compass box, including date/days, in its display's pixels.
    // A changed owner or a hidden HUD cannot reserve space for an obsolete compass.
    std::optional<std::array<uint32_t, 6>> labelCompass() const
    {
        return labelCompassOwner_ == owner() && frame_.available ? labelCompass_ : std::nullopt;
    }
    // The box the HUD's compass covers, for this revision of the map: the engine keeps its labels out of it.
    bool offer(const storm::ui::hud::MapLabelArea &label)
    {
        if (label.revision != revision_ || !frame_.available)
            return false;
        if (label.area)
        {
            const auto &area = *label.area;
            if (!area[0] || !area[1] || !area[4] || !area[5] || area[2] + area[4] > area[0] ||
                area[3] + area[5] > area[1])
                return false;
        }
        labelCompass_ = label.area;
        labelCompassOwner_ = owner();
        return true;
    }
    // A command of this revision of the map: one at a time.
    bool offer(const storm::ui::hud::MapCommand &command)
    {
        if (command.revision != revision_ || !frame_.available || !frame_.commandsReady || !pending_.empty())
            return false;
        for (const auto &offered : frame_.commands)
            if (offered.id == command.id)
            {
                pending_ = offered.id;
                pendingRequest_ = command.request;
                return true;
            }
        return false;
    }
    struct Taken
    {
        std::string id;
        uint64_t request = 0;
    };
    std::optional<Taken> take()
    {
        if (pending_.empty())
            return {};
        if (!frame_.available || !frame_.commandsReady)
        {
            drop("the map's commands are not ready");
            return {};
        }
        Taken taken{std::exchange(pending_, {}), std::exchange(pendingRequest_, 0)};
        // Consume the lease before native callbacks can change or destroy a scene.
        ++revision_;
        return taken;
    }
    storm::ui::hud::WorldMap hud() const
    {
        const auto reading = [](const auto &value) { return value ? hudReading(*value) : -1.0; };
        const auto count = [](const std::optional<int> &value) { return value.value_or(-1); };
        const auto &i = frame_.instruments;
        storm::ui::hud::WorldMap out;
        out.known = true;
        out.available = frame_.available;
        out.revision = revision_;
        out.activity = activity_;
        out.gameplay_revision = frame_.gameplayRevision;
        out.mask = frame_.availableMask();
        out.instruments = frame_.instrumentsVisible;
        out.ships_visible = frame_.shipsVisible;
        out.commands_ready = frame_.commandsReady;
        out.date = i.date;
        out.coordinates = i.coordinates;
        out.hour = reading(i.hour);
        out.wind_force = reading(i.windForce);
        out.morale = reading(i.morale);
        out.speed = reading(i.speed);
        out.time_scale = reading(i.timeScale);
        out.angles = i.windAngle && i.heading;
        out.wind_angle = i.windAngle ? hudReading(*i.windAngle) : 0.0;
        out.heading = i.heading ? hudReading(*i.heading) : 0.0;
        out.food_days = count(i.foodDays);
        out.rum_days = count(i.rumDays);
        out.flag = count(i.flag);
        out.storm = count(i.stormState);
        out.moon_phase = count(i.moonPhase);
        if (i.dayHours)
            std::tie(out.dawn, out.dusk) = *i.dayHours;
        for (const auto &s : frame_.ships)
            out.fleet.push_back({s.id, s.name, s.type, reading(s.hull), reading(s.sails),
                                 s.crew && s.crewMax && *s.crewMax > 0
                                     ? std::min(1.0, static_cast<double>(*s.crew) / *s.crewMax)
                                     : -1.0,
                                 hudPicture(s.picture)});
        for (const auto &c : frame_.commands)
            out.commands.push_back({c.id, c.label});
        return out;
    }
};
inline WorldMapHud worldMapHud;
// Implemented beside WM_INTERFACE so dispatch retains its actual command list.
std::string worldMapContext(uint64_t owner);
void readWorldMapCommands(WorldMapFrame &frame);
bool executeWorldMapCommand(const std::string &id);
} // namespace storm::android
