// Host checks for the world-map HUD: the consumer of channel "worldMap" and the scene alone
// (what each synthetic state shows, which engine input each event sends, what waits
// to be drawn), then the touches of the runtime with the small synthetic pack.
#include "hud_bench.h"

static bool Sent(const FakeMount &mount, const std::string &command)
{
    for (const std::string &sent : mount.commands)
        if (sent == command)
            return true;
    return false;
}

static void CheckConsumer()
{
    Bench bench;
    const WorldMap &map = bench.hud.Current().world_map;
    bench.Feed("worldMap", "world-map");
    Expect(map.known && map.available && map.revision == 41 && map.activity == 2 && map.mask == 7 &&
               map.fleet.size() == 2 && map.commands.size() == 1,
           "worldMap: the frame is read");
    Expect(map.food_days == 18 && map.rum_days == 4 && map.flag == 2 && std::abs(map.speed - 0.7) < 1e-9 &&
               map.angles && map.coordinates == "--",
           "worldMap: the instruments");
    Expect(map.storm == 0 && map.moon_phase == 1, "worldMap: the storm zone and the moon");
    size_t errors = 0;
    const auto rejected = [&](Json state, const std::string &what) {
        bench.Set("worldMap", state);
        Expect(bench.mount.errors.size() == ++errors && !bench.hud.Current().world_map.known,
               what + " is rejected aloud");
    };
    Json bad = State("world-map");
    bad["commands"][0]["id"] = "";
    rejected(bad, "worldMap: an unnamed command");
    bad = State("world-map");
    bad["moonPhase"] = 8;
    rejected(bad, "worldMap: a moon phase past the game's eight");
    bad = State("world-map");
    bad.erase("ships");
    rejected(bad, "worldMap: a frame without its fleet list");
    rejected(Json(4), "worldMap: a number");
}

static void CheckScene()
{
    Bench bench;
    FakeMount &mount = bench.mount;
    bench.Feed("gameplay", "gameplay-worldmap");
    bench.Feed("worldMap", "world-map");
    const Json &shown = mount.shown.elements;
    Expect(mount.visible && mount.shown.layout == "world-map", "the world-map layout");
    for (const char *id : {"fleet", "pause", "camera", "helm", "sail", "compass", "context"})
        Expect(mount.Has(id), std::string("the world map shows ") + id);
    Expect(!mount.Has("details") && !mount.Has("attack") && !mount.Has("storm"), "no details, no encounter buttons");
    const Json &ships = shown["fleet"]["ships"];
    Expect(ships.size() == 2 && ships[0]["name"] == "Аделина" &&
               ships[0]["texture"] == "battle_interface/ship_icons2" && ships[0]["uv"] == "0.5,0.25,0.5625,0.3125" &&
               ships[0]["bars"] ==
                   Json::parse(
                       R"([{"kind":"health","value":82},{"kind":"crew","value":50},{"kind":"sails","value":90}])") &&
               ships[1]["bars"][0]["value"] == 74 && ships[1]["tap"] == true,
           "the squadron: the map signs' ship pictures with hull, sails and crew inside: " + ships.dump());
    // A tap on a ship opens the ship screen on it.
    bench.Press("fleet", "ship", "1");
    const auto subject = bench.hud.TakeSubject("ship");
    Expect(mount.Last() == "shortcut:shipscreen" && subject && subject->position == 1,
           "a tap on a ship opens the ship screen on that ship: " + mount.Last());
    bench.Press("fleet", "ship", "5");
    Expect(mount.Last() == "shortcut:shipscreen" && !bench.hud.TakeSubject("ship"),
           "a ship that is not there opens nothing");
    Expect(shown["compass"]["course"] == 130 && shown["compass"]["windFrom"] == 140 && shown["compass"]["wind"] == 82 &&
               shown["compass"]["time"] == "12:31" && shown["compass"]["days"] == "18" &&
               shown["compass"]["sky"] == "sun" && shown["compass"]["skyAt"] == 12 &&
               !shown["compass"].contains("moon") && !shown["compass"].contains("weather") &&
               shown["sail"]["value"] == 70,
           "compass and lever: " + shown["compass"].dump());
    Expect(shown["context"]["icon"] == "sea-exit" && shown["context"]["variant"] == "primary",
           "going to sea in the corner");

    // nothing is activated before it is drawn
    bench.Press("context");
    Expect(!Sent(mount, "worldmap:41\tactivate\tEnterToSea"), "a command that was not drawn cannot be tapped");
    bench.Frame();

    // the compass shows the details beside it
    bench.Press("compass", "toggle", "1");
    Expect(mount.Has("details") && shown["compass"]["expanded"] == true && shown["detail-rows"]["items"].size() == 7,
           "a tap on the compass: the details");
    const Json &rows = shown["detail-rows"]["items"];
    Expect(rows[0]["v"] == "130°" && rows[1]["v"] == "hud.map.windFrom · 82%" && rows[2]["k"] == "hud.map.food" &&
               rows[6]["v"] == "hud.map.noSextant",
           "the details read the compass in words: " + rows.dump());
    bench.Press("compass", "toggle", "0");
    Expect(!mount.Has("details"), "a second tap hides them");

    // helm, lever, camera are held
    bench.Press("helm", "change", "70");
    Expect(mount.Last() == "hold:9\tWMapShipTurnRight\t1\t53", "the helm, as hard as it is pulled: " + mount.Last());
    bench.Press("helm", "change", "30");
    Expect(mount.Last() == "hold:9\tWMapShipTurnRight\t1\t10" && !Sent(mount, "hold:9\tWMapShipTurnRight\t0"),
           "eased back, the same turn softer: " + mount.Last());
    bench.Press("helm", "change", "-70");
    Expect(Sent(mount, "hold:9\tWMapShipTurnRight\t0") && mount.Last() == "hold:9\tWMapShipTurnLeft\t1\t53",
           "over to the other side: " + mount.Last());
    bench.Press("helm", "change", "0");
    Expect(mount.Last() == "hold:9\tWMapShipTurnLeft\t0", "amidships: " + mount.Last());
    bench.Press("sail", "hold", "up");
    Expect(mount.Last() == "hold:9\tWMapShipSailUp\t1", "lever up: " + mount.Last());
    bench.Press("sail", "hold", "down");
    Expect(Sent(mount, "hold:9\tWMapShipSailUp\t0") && mount.Last() == "hold:9\tWMapShipSailDown\t1",
           "lever down: " + mount.Last());
    bench.Press("sail", "hold", "");
    Expect(mount.Last() == "hold:9\tWMapShipSailDown\t0" && mount.slots.back().empty(),
           "lever let go: " + mount.Last());
    // A push and its release are two commands: in one engine loop the release must not replace the push.
    bench.Press("sail", "hold", "up");
    const std::string push = mount.slots.back();
    bench.Press("sail", "hold", "");
    Expect(!push.empty() && mount.slots.back().empty() && mount.Last() == "hold:9\tWMapShipSailUp\t0",
           "a short push keeps its own slot: " + push);
    bench.Press("sail", "hold", "up");
    Expect(mount.slots.back() != push && !mount.slots.back().empty(),
           "the next push is a new slot: " + mount.slots.back());
    bench.Press("sail", "hold", "");
    bench.Press("camera", "down");
    bench.Press("camera", "up");
    Expect(Sent(mount, "hold:9\tWMapCameraSwitch\t1") && mount.Last() == "hold:9\tWMapCameraSwitch\t0",
           "the camera button");
    bench.Press("pause");
    Expect(mount.Last() == "shortcut:mainmenu", "menu: " + mount.Last());

    // a command, once
    bench.Press("context");
    Expect(mount.Last() == "worldmap:41\tactivate\tEnterToSea", "going to sea: " + mount.Last());
    const size_t sent = mount.commands.size();
    bench.Press("context");
    Expect(mount.commands.size() == sent, "a second tap waits for the engine");

    // an encounter: three commands in their places
    bench.Feed("worldMap", "world-map-encounter");
    Expect(mount.Has("attack") && mount.Has("storm") && shown["attack"]["variant"] == "combat" &&
               shown["storm"]["icon"] == "wind",
           "the encounter's buttons");
    Expect(shown["compass"]["days"] == "2" && shown["compass"]["sky"] == "moon" && shown["compass"]["skyAt"] == 90 &&
               shown["compass"]["moon"] == 1 && shown["compass"]["weather"] == "storm",
           "two days of food; at nine the moon rises in the east; the storm zone: " + shown["compass"].dump());
    bench.Frame();
    bench.Press("attack");
    Expect(mount.Last() == "worldmap:44\tactivate\tEnterToAttack", "attack: " + mount.Last());
    Json mod = State("world-map");
    mod["commands"] = Json::array({Json{{"id", "EnterToMoon"}, {"label", "Fly to the moon"}}});
    bench.Set("worldMap", mod);
    Expect(shown["context"]["icon"] == "actions" && shown["context"]["label"] == "Fly to the moon",
           "a mod's command in the corner with a plain icon: " + shown["context"].dump());
}

// The sky over the compass: the sun and the moon cross the dome by the game's day (6…21) and
// night, the weather is the storm zone, and a missing value draws nothing and says so.
static void CheckSky()
{
    Bench bench;
    FakeMount &mount = bench.mount;
    bench.Feed("gameplay", "gameplay-worldmap");
    Json compass;
    const auto show = [&](double hour, Json storm, Json moon) {
        Json state = State("world-map");
        state["hour"] = hour;
        state["stormState"] = storm;
        state["moonPhase"] = moon;
        bench.Set("worldMap", state);
        compass = mount.shown.elements.value("compass", Json::object());
    };
    show(6.0, 0, 1);
    Expect(compass["sky"] == "sun" && compass["skyAt"] == 90, "sunrise at the east end: " + compass.dump());
    show(13.5, 0, 1);
    Expect(compass["sky"] == "sun" && compass["skyAt"] == 0 && !compass.contains("weather"),
           "noon high in the dome, no storm zone: no weather: " + compass.dump());
    show(20.99, 1, 1);
    Expect(compass["sky"] == "sun" && compass["skyAt"] == -90 && compass["weather"] == "storm",
           "sunset at the west end, a storm: " + compass.dump());
    show(1.5, 2, 0);
    Expect(compass["sky"] == "moon" && compass["skyAt"] == 0 && compass["moon"] == 0 && compass["weather"] == "tornado",
           "the new moon at half past one, a storm with a tornado: " + compass.dump());
    const size_t errors = mount.errors.size();
    show(3.0, 0, nullptr);
    Expect(compass["sky"] == "none" && !compass.contains("moon") && mount.errors.size() == errors + 1,
           "no moon phase at night: no moon, said aloud");
    show(12.0, nullptr, 3);
    Expect(compass["sky"] == "sun" && !compass.contains("weather") && mount.errors.size() == errors + 2,
           "no storm state: no weather, said aloud");
    Json dark = State("world-map");
    dark.erase("dawn");
    bench.Set("worldMap", dark);
    compass = mount.shown.elements.value("compass", Json::object());
    Expect(compass["sky"] == "none" && mount.errors.size() == errors + 3, "no dawn and dusk: no sky, said aloud");
}

// The time acceleration as at sea: the scale the engine runs at, a tap asks for the next step.
static void CheckTime()
{
    Bench bench;
    FakeMount &mount = bench.mount;
    bench.Feed("gameplay", "gameplay-worldmap");
    bench.Feed("worldMap", "world-map");
    Expect(!mount.Has("time"), "no time steps bound: no time button");
    Json gameplay = State("gameplay-worldmap");
    for (const char *step : {"TimeScaleX1", "TimeScaleX2", "TimeScaleX4", "TimeScaleX8"})
        gameplay["capabilities"].push_back(step);
    bench.Set("gameplay", gameplay);
    Expect(!mount.Has("time"), "the steps without the engine's scale: no time button");
    Json map = State("world-map");
    map["timeScale"] = 1;
    bench.Set("worldMap", map);
    const Json &shown = mount.shown.elements;
    Expect(mount.Has("time") && shown["time"]["badge"] == "×1" && shown["time"]["variant"] == "normal",
           "the world map's time scale: " + shown["time"].dump());
    bench.Press("time");
    Expect(mount.Last() == "gameplay:9\tTimeScaleX2", "x1 → x2: " + mount.Last());
    map["timeScale"] = 2.5; // the scripts' limit on the world map
    bench.Set("worldMap", map);
    Expect(shown["time"]["badge"] == "×2.5" && shown["time"]["variant"] == "active",
           "the limited scale is shown as it is: " + shown["time"].dump());
    bench.Press("time");
    Expect(mount.Last() == "gameplay:9\tTimeScaleX4",
           "x2.5 counts as x2: the next step is asked for, the engine goes round at its limit: " + mount.Last());
}

static void CheckLabelArea()
{
    Bench bench;
    bench.mount.boxes["compass "] = {702, 15, 240, 219, 0, 0, 960, 540, 1.5f};
    bench.Feed("gameplay", "gameplay-worldmap");
    bench.Feed("worldMap", "world-map");
    bench.Frame();
    Expect(Sent(bench.mount, "worldmap:41\tlabelarea\t960:540:702:15:240:219"),
           "the drawn compass reserves its measured full date/disc/days box");
    const auto sent = bench.mount.commands.size();
    bench.Frame();
    Expect(bench.mount.commands.size() == sent, "unchanged compass geometry is not resent");
    bench.mount.boxes["compass "] = {1022, 15, 240, 219, 0, 0, 1280, 720, 1.5f};
    bench.Frame();
    Expect(Sent(bench.mount, "worldmap:41\tlabelarea\t1280:720:1022:15:240:219"),
           "resizing updates the reservation even without a telemetry revision change");
    bench.mount.boxes.clear();
    bench.Frame();
    Expect(Sent(bench.mount, "worldmap:41\tlabelarea\tnone"), "an unmeasurable compass clears its old reservation");
    bench.mount.boxes["compass "] = {702, 15, 240, 219, 0, 0, 960, 540, 1.5f};
    bench.Frame();
    bench.mount.commands.clear();
    bench.Feed("presentation", "presentation-menu");
    Expect(Sent(bench.mount, "worldmap:41\tlabelarea\tnone"), "hiding the HUD clears its reservation too");
}

int main(int argc, char **argv)
{
    states_directory = argc > 1 ? argv[1] : "tests/states";
    CheckConsumer();
    CheckScene();
    CheckSky();
    CheckTime();
    CheckLabelArea();
    if (failures)
        std::fprintf(stderr, "%d world-map HUD check(s) failed\n", failures);
    else
        std::printf("world-map HUD checks passed\n");
    return failures ? 1 : 0;
}
