// The world-map HUD: fleet, compass (a tap shows the details beside it), helm slider, sail
// lever, camera, time acceleration (as at sea), menu and the map's commands (go to sea, enter a port or a ship, attack,
// the storm). Layout scene `world-map` of hud-layout.json.
//
// Input goes back as the HUD's commands (storm_ui/hud_command.h): Hold (WMapShipTurnLeft/Right as hard as the
// helm is pulled, WMapShipSailUp/Down, WMapCameraSwitch), MapCommand, MapLabelArea, GameplayControl (the time
// button, BuildTimeScale) and Shortcut (mainmenu, shipscreen).
#include "hud.h"
#include "app_ui_names.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace storm::ui::hud
{

namespace
{

constexpr double kTapSeconds = 1.5; // a tapped command waits this long for the engine to act
constexpr double kPi = 3.14159265358979323846;
// The game's day: its hourly weathers 6…20 are day, 21…5 night (weather/Init: Weathers[].Night).
constexpr double kDawn = 6.0, kDusk = 21.0;

using screens::StringKey;

enum Drawn : unsigned
{
    kInstruments = 1,
    kShips = 2,
    kCommands = 4,
};

struct Slot
{
    const char *element, *icon, *variant;
};

// Where each command sits: going on (to sea, a port,
// a ship) in the corner, the fight beside it, the storm further in.
Slot SlotOf(const std::string &id)
{
    if (id == "EnterToAttack" || id == "EnterToEnemy")
        return {"attack", "swords", "combat"};
    if (id == "EnterToStorm")
        return {"storm", "wind", "normal"};
    if (id == "EnterToIsland")
        return {"context", "port", "primary"};
    if (id == "EnterToShip")
        return {"context", "ship", "primary"};
    return {"context", "sea-exit", "primary"};
}

struct Weather
{
    const char *id, *word;
};
const Weather kWeathers[] = {{"clear", StringKey("hud.map.weather.clear")},
                             {"clouds", StringKey("hud.map.weather.clouds")},
                             {"rain", StringKey("hud.map.weather.rain")},
                             {"storm", StringKey("hud.map.weather.storm")},
                             {"tornado", StringKey("hud.map.weather.tornado")}};

// What the sky over the compass shows, worst first: the storm zone the ship is in
// (WdmPlayerShip::TestInStorm), then the rain the sea will have (rainState).
const Weather *WeatherOf(const WorldMap &map)
{
    if (map.storm < 0 || map.rain < 0)
        return nullptr;
    if (map.storm > 0)
        return &kWeathers[map.storm == 2 ? 4 : 3];
    return &kWeathers[map.rain];
}

const char *const kPoints[] = {
    StringKey("hud.map.dir.n"), StringKey("hud.map.dir.ne"), StringKey("hud.map.dir.e"), StringKey("hud.map.dir.se"),
    StringKey("hud.map.dir.s"), StringKey("hud.map.dir.sw"), StringKey("hud.map.dir.w"), StringKey("hud.map.dir.nw"),
};

// The engine's flag index (WorldMap nationFlagIndex).
const char *const kFlags[] = {StringKey("hud.map.flag.holland"), StringKey("hud.map.flag.england"),
                              StringKey("hud.map.flag.france"), StringKey("hud.map.flag.spain"),
                              StringKey("hud.map.flag.pirates")};

int Degrees(double radians)
{
    const long whole = std::lround(radians * 180.0 / kPi);
    return static_cast<int>(((whole % 360) + 360) % 360);
}

std::string Clock(double hour)
{
    const int minutes = static_cast<int>(std::floor(hour * 60.0)) % 1440;
    char text[16];
    std::snprintf(text, sizeof(text), "%02d:%02d", minutes / 60, minutes % 60);
    return text;
}

std::string Percent(double share)
{
    return std::to_string(static_cast<int>(std::lround(std::clamp(share, 0.0, 100.0)))) + "%";
}

class WorldMapScene final : public Scene
{
  public:
    explicit WorldMapScene(Hud &hud) : hud_(hud)
    {
    }

    void Build(const States &states, View &view, Input &input) override
    {
        const Gameplay &gameplay = states.gameplay;
        const WorldMap *map = Current(states);
        view.layout = "world-map";
        view.log = true;
        const Json lines = hud_.LogLines();
        if (!lines.empty())
            view.elements["log"] = Json{{"lines", lines}};

        view.elements["pause"] = Json::object();
        input.On("pause", "click", [this](const Event &) { hud_.Send(Shortcut{app::shortcut::mainmenu}); });
        view.elements["ship"] = Json::object(); // the ship screen, as at sea
        input.On("ship", "click", [this](const Event &) { hud_.Send(Shortcut{app::shortcut::shipscreen}); });
        if (gameplay.Has("WMapCameraSwitch"))
        {
            view.elements["camera"] = Json::object();
            input.On("camera", "down", [this](const Event &) { holds_.Hold("WMapCameraSwitch", true); });
            input.On("camera", "up", [this](const Event &) { holds_.Hold("WMapCameraSwitch", false); });
            input.Ignore("camera", "click");
        }
        BuildTimeScale(hud_, gameplay, map ? map->time_scale : -1.0, view, input);
        if (gameplay.Has("WMapShipTurnLeft") || gameplay.Has("WMapShipTurnRight"))
        {
            view.elements["helm"] = Json::object();
            input.On("helm", "change",
                     [this](const Event &event) { holds_.Helm(event.value, "WMapShipTurnLeft", "WMapShipTurnRight"); });
        }
        else
            hud_.Error("world-map HUD: the engine binds no helm (WMapShipTurnLeft / WMapShipTurnRight); no helm");
        if (gameplay.Has("WMapShipSailUp") || gameplay.Has("WMapShipSailDown"))
        {
            // The lever shows how fast the ship sails; pushed up or down it holds SailUp / SailDown.
            Json lever = Json::object();
            if (map && map->speed >= 0.0)
                lever["value"] = static_cast<int>(std::lround(map->speed * 100.0));
            view.elements["sail"] = std::move(lever);
            input.On("sail", "hold", [this](const Event &event) { Sails(event.value); });
        }
        if (!map)
        {
            details_ = false;
            return;
        }
        BuildFleet(*map, view, input);
        BuildCompass(*map, view, input);
        BuildCommands(*map, view, input);
    }

    void Hidden(const States &states) override
    {
        Uncover(states);
    }

    double Tick(const States &states, double now) override
    {
        double next = kIdle;
        holds_.Forget(states);
        if (!tapped_.empty())
        {
            if (now >= tapped_until_)
            {
                tapped_.clear();
                hud_.Rebuild();
            }
            else
                next = std::min(next, tapped_until_ - now);
        }
        return next;
    }

    void Drawn(const States &states) override
    {
        const View &shown = hud_.Shown();
        const WorldMap *map = hud_.Interactive() ? Current(states) : nullptr;
        unsigned mask = 0;
        if (map)
        {
            mask |= shown.elements.contains("compass") ? kInstruments : 0;
            mask |= shown.elements.contains("fleet") ? kShips : 0;
            bool all = map->commands_ready && !map->commands.empty();
            for (const WorldMap::Command &command : map->commands)
                all = all && shown.elements.contains(SlotOf(command.id).element);
            mask |= all ? kCommands : 0;
            mask &= map->mask;
        }
        CoverLabels(map, shown);
        Follow(states.world_map, mask);
    }

    void Released(const States &states) override
    {
        holds_.Release(states.gameplay);
    }

    void Suspended(const States &states) override
    {
        Uncover(states);
        tapped_.clear();
    }

  private:
    // The frame of the gameplay the HUD shows.
    static const WorldMap *Current(const States &states)
    {
        const WorldMap &map = states.world_map;
        return map.known && map.available && map.gameplay_revision == states.gameplay.revision ? &map : nullptr;
    }

    static std::string Owner(const WorldMap &map)
    {
        return std::to_string(map.revision) + "\t" + std::to_string(map.activity);
    }

    // The squadron column, as the map's own ship signs: the flagship first, each ship's picture
    // ringed by its hull, crew and sails (sea.cpp draws the same cells). A tap opens the ship
    // screen on that ship.
    void BuildFleet(const WorldMap &map, View &view, Input &input)
    {
        if (!map.ships_visible || map.fleet.empty())
            return;
        Json ships = Json::array();
        for (const WorldMap::Vessel &vessel : map.fleet)
        {
            if (vessel.picture.texture.empty())
                hud_.Log("world-map HUD: the engine gives no picture for the ship " + vessel.name + " (" + vessel.type +
                         "): its placeholder stays");
            ships.push_back(MemberCell(vessel.name, vessel.picture,
                                       {{"health", vessel.hull < 0.0 ? -1.0 : vessel.hull / 100.0},
                                        {"crew", vessel.crew},
                                        {"sails", vessel.sails < 0.0 ? -1.0 : vessel.sails / 100.0}},
                                       true));
        }
        view.elements["fleet"] = Json{{"ships", ships}};
        input.On("fleet", "ship", [this](const Event &event) {
            const WorldMap *now = Current(hud_.Current());
            const int index = std::atoi(event.value.c_str());
            if (!now || index < 0 || static_cast<size_t>(index) >= now->fleet.size())
                return;
            hud_.RequestSubject({"ship", -1, index});
            hud_.Send(Shortcut{app::shortcut::shipscreen});
        });
    }

    void BuildCompass(const WorldMap &map, View &view, Input &input)
    {
        if (!map.instruments || map.hour < 0.0 || !map.angles || map.wind_force < 0.0)
        {
            details_ = false;
            return;
        }
        const int course = Degrees(map.heading), from = Degrees(map.wind_angle + kPi);
        Json compass = {{"date", map.date},
                        {"time", Clock(map.hour)},
                        {"course", course},
                        {"windFrom", from},
                        {"wind", static_cast<int>(std::lround(map.wind_force * 100.0))},
                        {"expanded", details_}};
        // The sky dome over the rose: the sun by day, the moon by night, rising at the east end
        // (+90°), highest at the top, setting at the west end (-90°).
        const bool day = map.hour >= kDawn && map.hour < kDusk;
        const double share = day ? (map.hour - kDawn) / (kDusk - kDawn)
                                 : std::fmod(map.hour - kDusk + 24.0, 24.0) / (24.0 - kDusk + kDawn);
        compass["sky"] = day ? "sun" : "moon";
        compass["skyAt"] = static_cast<int>(std::lround(90.0 - 180.0 * share));
        if (!day && map.moon_phase < 0)
        {
            hud_.Error("world-map HUD: the engine gives no moonPhase at night; the compass draws no moon");
            compass["sky"] = "none";
        }
        else if (!day)
            compass["moon"] = map.moon_phase;
        const Weather *weather = WeatherOf(map);
        if (weather)
            compass["weather"] = weather->id;
        else
            hud_.Error("world-map HUD: the engine gives no stormState or rainState; the compass draws no weather");
        if (map.food_days >= 0)
        {
            compass["days"] = std::to_string(map.food_days);
            compass["daysTone"] = map.food_days <= 3 ? "danger" : "normal";
        }
        view.elements["compass"] = std::move(compass);
        input.On("compass", "toggle", [this](const Event &event) {
            details_ = event.value == "1";
            hud_.Rebuild();
        });
        if (!details_)
            return;
        // The readings the compass draws as pictures, in words.
        const std::string days = " " + hud_.Text("common.days_short");
        Json items = Json::array();
        items.push_back(Json{{"k", hud_.Text("hud.map.course")}, {"v", std::to_string(course) + "°"}});
        std::string wind = hud_.Text("hud.map.windFrom");
        const size_t at = wind.find("{dir}");
        if (at != std::string::npos)
            wind.replace(at, 5, hud_.Text(kPoints[((from + 22) % 360) / 45]));
        items.push_back(Json{{"k", hud_.Text("hud.map.wind")}, {"v", wind + " · " + Percent(map.wind_force * 100.0)}});
        if (weather)
            items.push_back(Json{{"k", hud_.Text("hud.map.weather")}, {"v", hud_.Text(weather->word)}});
        if (map.food_days >= 0)
            items.push_back(Json{{"k", hud_.Text("hud.map.food")},
                                 {"v", std::to_string(map.food_days) + days},
                                 {"tone", map.food_days <= 3 ? "danger" : ""}});
        if (map.rum_days >= 0)
            items.push_back(Json{{"k", hud_.Text("hud.map.rum")}, {"v", std::to_string(map.rum_days) + days}});
        if (map.morale >= 0.0)
            items.push_back(Json{{"k", hud_.Text("hud.map.morale")}, {"v", Percent(map.morale)}});
        if (map.flag >= 0 && map.flag < static_cast<int>(std::size(kFlags)))
            items.push_back(Json{{"k", hud_.Text("hud.map.flag")}, {"v", hud_.Text(kFlags[map.flag])}});
        // The engine writes dashes while the fleet has no sextant.
        const bool known = !map.coordinates.empty() && map.coordinates.find("--") == std::string::npos;
        items.push_back(Json{{"k", hud_.Text("hud.map.coordinates")},
                             {"v", known ? map.coordinates : hud_.Text("hud.map.noSextant")}});
        view.elements["details"] = Json::object();
        view.elements["detail-rows"] = Json{{"items", items}};
    }

    void BuildCommands(const WorldMap &map, View &view, Input &input)
    {
        if (!map.commands_ready)
            return;
        for (const WorldMap::Command &command : map.commands)
        {
            const Slot slot = SlotOf(command.id);
            view.elements[slot.element] = Json{{"icon", slot.icon},
                                               {"label", command.label},
                                               {"variant", slot.variant},
                                               {"pressed", tapped_ == command.id}};
            input.On(slot.element, "click", [this, id = command.id](const Event &) { Activate(id); });
        }
    }

    // Exactly the command that is drawn, once per offer.
    void Activate(const std::string &id)
    {
        const WorldMap *map = Current(hud_.Current());
        if (!hud_.Interactive() || !map || !map->commands_ready || !tapped_.empty() || drawn_owner_ != Owner(*map) ||
            !(drawn_ & kCommands))
            return;
        tapped_ = id;
        tapped_until_ = hud_.Now() + kTapSeconds;
        Released(hud_.Current());
        hud_.Send(MapCommand{map->revision, id});
        hud_.Rebuild();
    }

    // What the drawn frame shows of the map's revision: a command can be tapped once it is drawn,
    // and a new revision means the engine acted on the tapped one.
    void Follow(const WorldMap &map, unsigned mask)
    {
        if (!map.known || !map.available)
        {
            drawn_ = 0;
            drawn_owner_.clear();
            return;
        }
        const std::string owner = Owner(map);
        drawn_ = mask;
        drawn_owner_ = owner;
        if (owner != tapped_owner_)
        {
            tapped_owner_ = owner;
            if (!tapped_.empty()) // the engine acted: a new revision
            {
                tapped_.clear();
                hud_.Rebuild();
            }
        }
    }

    void Uncover(const States &states)
    {
        CoverLabels(nullptr, hud_.Shown());
        Follow(states.world_map, 0);
    }

    void CoverLabels(const WorldMap *map, const View &shown)
    {
        if (!map || !shown.elements.contains("compass"))
        {
            if (!label_owner_.empty() && label_geometry_ != "none")
                hud_.Send(MapLabelArea{label_revision_, std::nullopt}, "worldmap-labelarea");
            label_geometry_ = "none";
            return;
        }
        Placement box;
        MapLabelArea area{map->revision, std::nullopt};
        if (hud_.Measure("compass", "", box) && box.screen_width > 0 && box.screen_height > 0 && box.width > 0 &&
            box.height > 0)
        {
            // Send the real component box, including date and days. Do not reproduce the
            // pack's anchors/safe-area arithmetic in the world-map renderer.
            const auto px = [](float value) { return static_cast<uint32_t>(std::lround(value)); };
            area.area = std::array<uint32_t, 6>{px(box.screen_width), px(box.screen_height), px(box.x), px(box.y),
                                                px(box.width),        px(box.height)};
        }
        const std::string owner = Owner(*map);
        std::string geometry = "none"; // what the engine was told last: the box, or none
        if (area.area)
            for (const uint32_t value : *area.area)
                geometry += ":" + std::to_string(value);
        if (owner != label_owner_ || geometry != label_geometry_)
        {
            label_owner_ = owner;
            label_revision_ = map->revision;
            label_geometry_ = geometry;
            hud_.Send(area, "worldmap-labelarea");
        }
    }

    void Sails(const std::string &dir)
    {
        holds_.Hold(dir == "up" ? "WMapShipSailDown" : "WMapShipSailUp", false);
        if (dir == "up" || dir == "down")
            holds_.Hold(dir == "up" ? "WMapShipSailUp" : "WMapShipSailDown", true);
        else
            holds_.Hold("WMapShipSailDown", false);
    }

    Hud &hud_;
    HoldSet holds_{hud_};
    bool details_ = false;
    unsigned drawn_ = 0;
    std::string drawn_owner_, tapped_owner_;
    std::string label_owner_, label_geometry_;
    uint64_t label_revision_ = 0;
    std::string tapped_;
    double tapped_until_ = 0.0;
};

} // namespace

std::unique_ptr<Scene> CreateWorldMapScene(Hud &hud)
{
    return std::make_unique<WorldMapScene>(hud);
}

} // namespace storm::ui::hud
