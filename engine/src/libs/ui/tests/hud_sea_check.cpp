// Host checks for the sea HUD, the scene of the touch interface's scripts (interface/touch/hud/sea.c) on the fixture
// game's program. First the consumers of its channels (ship, seaOrders, seaQuickAction) and the scene alone: what
// each synthetic state shows, which engine input each event sends, what is acknowledged as drawn. Then the whole
// runtime with the application's pack: which touches the UI claims at sea and what real gestures send.
#include "hud_scripts.h"

#include <algorithm>
#include <chrono>
#include <RmlUi/Core.h>

constexpr double kRaiseWait = 3.5; // longer than the sea HUD waits for the game to raise a held spyglass

static const ScriptPack *app_pack = nullptr;

// A bench whose HUD draws the scripts' sea scene with the application's pack.
struct SeaBench : Bench
{
    SeaBench()
    {
        app_pack->Attach(*this);
    }
};

static bool Sent(const FakeMount &mount, const std::string &command)
{
    for (const std::string &sent : mount.commands)
        if (sent == command)
            return true;
    return false;
}

static size_t Count(const FakeMount &mount, const std::string &command)
{
    size_t count = 0;
    for (const std::string &sent : mount.commands)
        count += sent == command ? 1 : 0;
    return count;
}

static void Sail(Bench &bench)
{
    bench.Feed("seaOrders", "sea-orders-idle");
    bench.Feed("gameplay", "gameplay-sailing");
    bench.Feed("ship", "ship");
    bench.Feed("seaQuickAction", "sea-quick-moor");
}

static void CheckConsumers()
{
    SeaBench bench;
    const States &states = bench.hud.Current();
    FakeMount &mount = bench.mount;

    bench.Feed("ship", "ship");
    const Ship &ship = states.ship;
    Expect(ship.known && ship.gameplay_revision == 9 && ship.hud_revision == 3 && ship.own.name == "Аделина" &&
               ship.fleet.size() == 3,
           "ship: the frame is read");
    Expect(std::abs(ship.own.hull - 0.82) < 1e-6 && std::abs(ship.fleet[2].hull - 0.41) < 1e-6 &&
               ship.fleet[1].character == 8,
           "ship: bars are shares of their maximum");
    Expect(ship.ammo == "balls" && ship.ammo_count == 88 && ship.ammo_counts[3] == 16 && !ship.ammo_low &&
               ship.sail_state == 1.0 && ship.can_set_sails,
           "ship: ammunition and sails");
    Expect(ship.captions && ship.navigator && ship.navigator_generation == 4 && ship.speed == 4.2 && ship.wind == 9.0 &&
               ship.cannon_charge == 1.0 && ship.location == "Мартиника",
           "ship: the readouts");
    Json partial = State("ship");
    partial["sequence"] = 21;
    for (const char *key : {"hull", "crewMax", "shipSpeedKnots", "ammo", "sailState", "cannonCharge"})
        partial.erase(key);
    bench.Set("ship", partial);
    Expect(states.ship.known && states.ship.own.hull < 0 && states.ship.own.crew < 0 && states.ship.speed < 0 &&
               states.ship.ammo.empty() && states.ship.sail_state < 0 && states.ship.cannon_charge < 0 &&
               mount.errors.empty(),
           "ship: what the engine does not know stays unknown");
    bench.Feed("ship", "ship-none");
    Expect(!states.ship.known && states.ship.hud_revision == 4 && mount.errors.empty(),
           "ship: no sea scene is not an error");
    size_t errors = 0;
    const auto rejected = [&](const char *channel, Json state, const std::string &what) {
        bench.Set(channel, state);
        Expect(mount.errors.size() == ++errors, what + " is rejected aloud");
    };
    Json bad = State("ship");
    bad["schemaVersion"] = 2;
    rejected("ship", bad, "ship: another schema");
    Expect(!states.ship.known, "ship: a rejected frame leaves no ship");
    bad = State("ship");
    bad.erase("fleet");
    rejected("ship", bad, "ship: a frame without the fleet list");
    bad = State("ship");
    bad["ammo"] = "harpoons";
    rejected("ship", bad, "ship: an unknown ammunition kind");
    bad = State("ship");
    bad["fleet"][0].erase("entityId");
    rejected("ship", bad, "ship: a companion without identity");
    rejected("ship", Json("sea"), "ship: a string");

    bench.Feed("seaOrders", "sea-orders-root");
    const SeaOrders &orders = states.sea_orders;
    Expect(orders.known && orders.active && orders.available && orders.complete && orders.scene == 6 &&
               orders.revision == 15 && orders.stage == "commands" && orders.recipient == "r501_1" &&
               orders.recipients.size() == 4 && orders.items.size() == 7,
           "seaOrders: the menu is read");
    Expect(orders.items[0].source == "BI_SailTo" && !orders.items[5].enabled && orders.items[5].cooldown == 0.4 &&
               orders.recipients[0].main,
           "seaOrders: items and recipients");
    bench.Feed("seaOrders", "sea-orders-unknown-kind");
    Expect(states.sea_orders.known && states.sea_orders.active && !states.sea_orders.complete &&
               mount.errors.size() == errors,
           "seaOrders: an unknown item kind leaves the menu to the engine, no error");
    bench.Set("seaOrders", nullptr);
    Expect(!states.sea_orders.known && mount.errors.size() == errors, "seaOrders: null = no menu");
    bad = State("sea-orders-root");
    bad["stage"] = "teleport";
    rejected("seaOrders", bad, "seaOrders: an unknown stage");
    bad = State("sea-orders-root");
    bad["items"][1]["id"] = "i0";
    rejected("seaOrders", bad, "seaOrders: a repeated item id");
    bad = State("sea-orders-root");
    bad["recipientId"] = "r9_9";
    rejected("seaOrders", bad, "seaOrders: a recipient that is not listed");
    bad = State("sea-orders-root");
    bad["selectedItemId"] = "i99";
    rejected("seaOrders", bad, "seaOrders: a selection that is not listed");
    bad = State("sea-orders-root");
    bad.erase("recipients");
    rejected("seaOrders", bad, "seaOrders: a menu without recipients");
    Expect(!states.sea_orders.known, "seaOrders: a rejected menu leaves none");

    bench.Feed("seaQuickAction", "sea-quick-board");
    Expect(states.sea_quick.known && states.sea_quick.available && states.sea_quick.action == "Board" &&
               states.sea_quick.revision == 32 && states.sea_quick.label == "Абордаж",
           "seaQuickAction: the offer is read");
    bench.Feed("seaQuickAction", "sea-quick-none");
    Expect(states.sea_quick.known && !states.sea_quick.available && states.sea_quick.action.empty(),
           "seaQuickAction: no offer");
    bad = State("sea-quick-moor");
    bad["revision"] = 40;
    bad["action"] = "EnterToSea";
    rejected("seaQuickAction", bad, "seaQuickAction: an action outside the five");
    Expect(!states.sea_quick.known, "seaQuickAction: a rejected offer leaves none");
    bad = State("sea-quick-moor");
    bad.erase("revision");
    rejected("seaQuickAction", bad, "seaQuickAction: an offer without its revision");
    bad = State("sea-quick-moor-map");
    bad["revision"] = 42;
    bad.erase("mapLabel");
    rejected("seaQuickAction", bad, "seaQuickAction: the map beside an action without its caption");
    bad = State("sea-quick-moor-map");
    bad["revision"] = 43;
    bad["action"] = "Map";
    rejected("seaQuickAction", bad, "seaQuickAction: the map beside itself");
}

// Sailing on one HUD: what is shown, the first frame, shortcuts, the helm, the sail lever, the ammunition fan, the
// minimap, event lines, the engine's indicators and the states that take elements away.
struct SailingRun
{
    SeaBench bench;
    FakeMount &mount = bench.mount;
    const Json &shown = mount.shown.elements;

    void Shown()
    {
        Sail(bench);
        Expect(mount.visible && mount.shown.layout == "sea" && !mount.shown.modal && mount.shown.sheet.empty(),
               "sailing: the sea layout");
        for (const char *id :
             {"fleet", "ship", "pause", "orders", "minimap", "sail", "helm", "context", "camera", "ammo", "fire"})
            Expect(mount.Has(id), std::string("sailing shows ") + id);
        Expect(!mount.Has("spyglass") && !mount.Has("log") && !mount.Has("ammo-1") && !mount.Has("map"),
               "sailing: no spyglass off the deck, no event lines, no fan, no map beside the mooring");
        const Json &ships = shown["fleet"]["ships"];
        Expect(ships.size() == 4 && ships[0]["name"] == "Аделина" &&
                   ships[0]["bars"] ==
                       Json::parse(
                           R"([{"kind":"health","value":82},{"kind":"crew","value":90},{"kind":"sails","value":95}])"),
               "the flagship first, its hull, sails and crew inside its picture: " + ships[0].dump());
        Expect(ships[0]["texture"] == "battle_interface/ship_icons2" && ships[0]["uv"] == "0.5,0.125,0.5625,0.1875" &&
                   ships[1]["name"] == "Сокол" && ships[3]["bars"][0]["value"] == 41 &&
                   ships[2]["name"] == "Ласточка" && ships[1]["tap"] == true,
               "then the companions, each the battle interface's ship picture: " + ships[1].dump());
        Expect(shown["minimap"]["place"] == "Мартиника" && shown["minimap"]["date"] == "7 октября 1654 · 09:06" &&
                   shown["minimap"]["speed"] == "4common.decimal_mark2" && !shown["minimap"].contains("wind") &&
                   !shown["minimap"].contains("time") && shown["minimap"]["native"] == true,
               "the minimap readouts: " + shown["minimap"].dump());
        Expect(shown["sail"]["value"] == 2 && shown["ammo"]["badge"] == "88" && shown["ammo"]["badgeTone"] == "gold" &&
                   shown["ammo"]["icon"] == "ammo" && !shown["fire"].contains("fill"),
               "sails, ammunition and loaded guns");
        Expect(shown["context"]["icon"] == "moor" && shown["context"]["label"] == "Причалить" &&
                   shown["context"]["variant"] == "primary",
               "the quick action with its own glyph");
        Expect(mount.errors.empty(), "sailing: no errors");
    }

    void FirstFrame()
    {
        // nothing can be activated before a frame is drawn
        bench.Press("context");
        Expect(!Sent(mount, "seaquick:31\tMoor"), "a quick action that was not drawn yet cannot be tapped");
        bench.Frame();
        Expect(Count(mount, "hudowner:sea") == 1 && Count(mount, "hudowner:worldmap") == 1 &&
                   Count(mount, "hudowner:land") == 1,
               "the HUD declares once which engine HUDs it presents");
    }

    void Shortcuts()
    {
        // shortcuts and tap controls
        bench.Press("ship");
        Expect(mount.Last() == "shortcut:shipscreen", "the ship button opens the ship screen: " + mount.Last());
        bench.Press("pause");
        Expect(mount.Last() == "shortcut:mainmenu", "menu: " + mount.Last());
        bench.Press("camera");
        Expect(mount.Last() == "gameplay:9\tSea_CameraSwitch", "camera: " + mount.Last());
        bench.Press("fire");
        Expect(mount.Last() == "gameplay:9\tShip_Fire", "fire: " + mount.Last());
        Expect(mount.shown.elements["time"]["badge"] == "×1" && mount.shown.elements["time"]["variant"] == "normal" &&
                   !mount.Has("time-slower") && !mount.Has("time-faster"),
               "the time scale the engine runs at: " + mount.shown.elements["time"].dump());
        bench.Press("time");
        Expect(mount.Last() == "gameplay:9\tTimeScaleX2", "x1 → x2: " + mount.Last());
        bench.Press("orders");
        Expect(mount.Last() == "orders:14\topen\t", "orders: " + mount.Last());
        bench.Press("fleet", "ship", "2");
        Expect(mount.Last() == "orders:14\topen\tr503_8",
               "a tap on a companion opens the orders for it: " + mount.Last());
        bench.Press("fleet", "ship", "0");
        Expect(mount.Last() == "orders:14\topen\tr501_1",
               "a tap on the flagship opens its own orders: " + mount.Last());
        const size_t before = mount.commands.size();
        bench.Press("fleet", "ship", "7");
        Expect(mount.commands.size() == before, "a companion that is not there opens nothing");

        // the quick action: exactly this action, once
        bench.Press("context");
        Expect(mount.Last() == "seaquick:31\tMoor", "the quick action activates exactly its action: " + mount.Last());
        bench.Press("context");
        Expect(Count(mount, "seaquick:31\tMoor") == 1, "a second tap on the same offer does nothing");

        bench.Feed("seaQuickAction", "sea-quick-board");
        Expect(mount.shown.elements["context"]["icon"] == "grapple" &&
                   mount.shown.elements["context"]["variant"] == "combat",
               "another offer, another glyph");
        bench.Press("context");
        Expect(!Sent(mount, "seaquick:32\tBoard"), "the new offer is not tapped before it is drawn");
        bench.Frame(0.3);
        bench.Press("context");
        Expect(mount.Last() == "seaquick:32\tBoard", "board: " + mount.Last());
        bench.Feed("seaQuickAction", "sea-quick-none");
        bench.Frame();
        Expect(!mount.Has("context"), "no offer, no button");
    }

    void Helm()
    {
        // the helm holds a turn as hard as it is pulled, until it is let go
        const size_t before_helm = mount.commands.size();
        bench.Press("helm", "change", "5");
        Expect(mount.commands.size() == before_helm, "the dead zone in the middle turns nothing");
        bench.Press("helm", "change", "15");
        Expect(mount.Last() == "hold:9\tShip_TurnRight\t1\t2" && mount.slots.back() == "hold-Ship_TurnRight-1",
               "just off the middle a slight turn: " + mount.Last());
        bench.Press("helm", "change", "60");
        Expect(mount.Last() == "hold:9\tShip_TurnRight\t1\t40" && !Sent(mount, "hold:9\tShip_TurnRight\t0"),
               "pulled further, the same hold turns harder (the curve: 60 of the travel is 40): " + mount.Last());
        bench.Frame(3.0);
        Expect(Count(mount, "hold:9\tShip_TurnRight\t1\t40") == 1,
               "a held helm is said once: the engine holds it until its release");
        bench.Press("helm", "change", "100");
        Expect(mount.Last() == "hold:9\tShip_TurnRight\t1\t100", "hard over: " + mount.Last());
        bench.Press("helm", "change", "-80");
        Expect(Sent(mount, "hold:9\tShip_TurnRight\t0") && mount.Last() == "hold:9\tShip_TurnLeft\t1\t68",
               "helm over to port: " + mount.Last());
        bench.Press("helm", "change", "0");
        Expect(mount.Last() == "hold:9\tShip_TurnLeft\t0", "helm amidships: " + mount.Last());
        bench.Press("helm", "change", "100");
        bench.hud.Released();
        Expect(mount.Last() == "hold:9\tShip_TurnRight\t0", "cancelled input lets the helm go: " + mount.Last());
        bench.Press("helm", "change", "100");
        Expect(mount.Last() == "hold:9\tShip_TurnRight\t1\t100",
               "and the same pull after it is said again: " + mount.Last());
        bench.Press("helm", "change", "0");
        const size_t held = mount.commands.size();
        bench.Frame(0.3);
        Expect(mount.commands.size() == held, "a released helm is silent");
    }

    void SailLever()
    {
        // the sail lever
        bench.Press("sail", "change", "1");
        Expect(mount.Last() == "sails:9\t1" && mount.shown.elements["sail"]["value"] == 1,
               "sails to half: " + mount.Last());
        bench.Frame(2.0);
        Expect(mount.shown.elements["sail"]["value"] == 1, "the lever holds the position until the game answers");
        bench.Answer(false);
        Expect(mount.shown.elements["sail"]["value"] == 2,
               "sails the game refused: the lever returns to what the ship has at once");
        bench.Press("sail", "change", "5");
        Expect(mount.Last() == "sails:9\t1", "a position the lever does not have is not sent");
    }

    void AmmunitionFan()
    {
        // the ammunition fan
        bench.Feed("seaQuickAction", "sea-quick-moor");
        Json newer = State("sea-quick-moor");
        newer["revision"] = 34;
        bench.Set("seaQuickAction", newer);
        bench.Press("ammo");
        const Json &fan = mount.shown.elements;
        Expect(mount.Has("ammo-1") && mount.Has("ammo-2") && mount.Has("ammo-3") && fan["ammo-1"]["icon"] == "grape" &&
                   fan["ammo-1"]["badge"] == "40" && fan["ammo-2"]["icon"] == "chain" &&
                   fan["ammo-3"]["icon"] == "bomb" && fan["ammo-3"]["badge"] == "16" &&
                   fan["ammo"]["variant"] == "active",
               "the fan offers the other three kinds with their counts");
        Expect(!mount.Has("context") && !mount.Has("camera") && mount.Has("fire") && mount.Has("helm"),
               "the fan takes the place of the buttons around it");
        bench.Frame(0.3);
        Expect(!mount.Has("context"), "the quick action is under the open fan");
        bench.Press("ammo-2");
        Expect(mount.Last() == "gameplay:9\thk_charge3" && !mount.Has("ammo-1") && mount.Has("context"),
               "a kind is chosen and the fan folds: " + mount.Last());
        bench.Press("ammo");
        bench.Frame(7.0);
        Expect(!mount.Has("ammo-1"), "an untouched fan folds by itself");
        bench.Press("ammo");
        bench.Press("ammo");
        Expect(!mount.Has("ammo-1"), "a second tap folds the fan");
    }

    void Minimap()
    {
        // the minimap: the engine's radar needs the place of the disc, then the word that it shows
        Expect(!Sent(mount, "navigator:9\t3\t4\tprepare\t1\t800\t400\t704\t84\t50\t1000\t1"),
               "no aperture before the disc is on the screen");
        mount.boxes["minimap .mm-disc"] = {654, 34, 100, 100, 0, 0, 800, 400, 1};
        bench.Frame();
        Expect(mount.Last() == "navigator:9\t3\t4\tprepare\t1\t800\t400\t704\t84\t50\t1000\t1" &&
                   mount.slots.back() == "navigator-prepare",
               "the aperture of the drawn disc: " + mount.Last());
        bench.Frame(5.0);
        Expect(Count(mount, "navigator:9\t3\t4\tprepare\t1\t800\t400\t704\t84\t50\t1000\t1") == 1 &&
                   !Sent(mount, "navigator:9\t3\t4\tvisible\t1"),
               "the aperture is said once per owner and place, after the engine drew there");
        Json drawn = State("ship");
        drawn["sequence"] = 22;
        drawn["navigatorLayoutId"] = 1;
        bench.Set("ship", drawn);
        bench.Frame(0.3);
        Expect(Sent(mount, "navigator:9\t3\t4\tvisible\t1"),
               "the engine drew into the aperture: the readouts are called visible");
        bench.Press("minimap", "zoom");
        Expect(mount.Last() == "navigator:9\t3\t4\tin", "a tap on the minimap zooms: " + mount.Last());
        drawn["sequence"] = 23;
        drawn["navigatorZoomIn"] = false;
        bench.Set("ship", drawn);
        bench.Press("minimap", "zoom");
        Expect(mount.Last() == "navigator:9\t3\t4\tout",
               "at the closest scale the next tap goes back out: " + mount.Last());
        mount.boxes["minimap .mm-disc"] = {554, 34, 100, 100, 0, 0, 800, 400, 1};
        bench.Frame(0.01);
        Expect(mount.Last() == "navigator:9\t3\t4\tprepare\t2\t800\t400\t604\t84\t50\t1000\t1",
               "a disc that moved is a new aperture, at once: " + mount.Last());
    }

    void EventLines()
    {
        // event lines; a fleet that wraps moves their band
        Expect(bench.hud.Notice("Якорь поднят", "info") && (bench.hud.Tick(), mount.Has("log")) &&
                   !mount.shown.elements["log"].contains("safeLeft"),
               "event lines top-centre");
        mount.boxes["fleet .item:first-child"] = {10, 60, 52, 52, 0, 0, 800, 400, 1};
        mount.boxes["fleet .item:last-child"] = {130, 60, 52, 52, 0, 0, 800, 400, 1};
        bench.Frame();
        bench.Frame();
        Expect(mount.shown.elements["log"]["safeLeft"] == 190,
               "the band of event lines begins after the last fleet column: " + mount.shown.elements["log"].dump());
        // A narrow screen (640 dp, the fleet in one or two columns): beside the top row the band
        // would be under 240 dp, so it drops under the row, between the fleet and the minimap. On a
        // wide one (915 dp) it stays pinned. The time button is at the bottom, not in the row.
        const auto top_row = [&](float width, float last_column) {
            mount.boxes["fleet .item:first-child"] = {10, 10, 56, 56, 0, 0, width, 360, 1, width, 360};
            mount.boxes["fleet .item:last-child"] = {last_column, 10, 56, 56, 0, 0, width, 360, 1, width, 360};
            mount.boxes["time "] = {width - 320, 296, 48, 48,    0,  0,
                                    width,       360, 1,  width, 360}; // at the bottom: not in the top row
            mount.boxes["ship "] = {width - 356, 10, 48, 48, 0, 0, width, 360, 1, width, 360};
            mount.boxes["pause "] = {width - 298, 10, 48, 48, 0, 0, width, 360, 1, width, 360};
            mount.boxes["orders "] = {width - 240, 10, 48, 48, 0, 0, width, 360, 1, width, 360};
            mount.boxes["minimap "] = {width - 182, 10, 172, 150, 0, 0, width, 360, 1, width, 360};
            bench.Frame();
            bench.Frame();
        };
        top_row(640, 10);
        Expect(mount.shown.elements["log"]["offset"] == 66 && mount.shown.elements["log"]["safeLeft"] == 74,
               "640 dp, one fleet column: the band drops: " + mount.shown.elements["log"].dump());
        top_row(640, 74);
        {
            const Json &log = mount.shown.elements["log"];
            Expect(log["offset"] == 66 && log["safeLeft"] == 138 && log["safeRight"] == 190,
                   "640 dp: the band drops under the top row, between the fleet and the minimap: " + log.dump());
        }
        top_row(915, 74);
        Expect(!mount.shown.elements["log"].contains("offset") && !mount.shown.elements["log"].contains("safeRight"),
               "915 dp: the band stays pinned to the top edge: " + mount.shown.elements["log"].dump());
        // «Под левую руку»: the same boxes mirrored (the fleet on the right, the top row and the minimap on the left);
        // the band is worked out the same way and its sides swap.
        mount.mirrored = true;
        top_row(640, 74); // the right-handed boxes, mirrored below
        for (const char *key :
             {"fleet .item:first-child", "fleet .item:last-child", "time ", "ship ", "pause ", "orders ", "minimap "})
        {
            Placement &box = mount.boxes[key];
            box.x = 640 - box.x - box.width;
        }
        bench.Frame();
        bench.Frame();
        {
            const Json &log = mount.shown.elements["log"];
            Expect(log["offset"] == 66 && log["safeLeft"] == 190 && log["safeRight"] == 138,
                   "left-handed 640 dp: the band drops between minimap and fleet: " + log.dump());
        }
        mount.mirrored = false;
    }

    void Indicators()
    {
        // what is not drawn any more gives the engine's indicators back
        mount.commands.clear();
        bench.Feed("presentation", "presentation-menu");
        Expect(!mount.visible && Sent(mount, "navigator:9\t3\t4\tclear"), "under a menu the radar's disc is withdrawn");
        mount.commands.clear();
        bench.Feed("presentation", "presentation-game");
        bench.Frame();
        Expect(mount.visible, "back in the game the HUD is drawn again");
        bench.Press("helm", "change", "100");
        mount.commands.clear();
        bench.Feed("presentation", "presentation-background");
        Expect(Sent(mount, "hold:9\tShip_TurnRight\t0"), "a game that lost the screen: the helm is let go");
        mount.commands.clear();
        bench.Press("fire");
        bench.Press("context");
        bench.Press("helm", "change", "100");
        Expect(mount.commands.empty(), "not live: no input goes out");
    }

    void StatesTakeAway()
    {
        // states that take elements away
        bench.Feed("presentation", "presentation-game");
        bench.Feed("ship", "ship-none");
        Expect(mount.visible && !mount.Has("fleet") && !mount.Has("minimap") && !mount.Has("sail") &&
                   !mount.Has("ammo") && mount.Has("fire") && mount.Has("helm"),
               "without telemetry only the controls of the gameplay state stay");
        Json other = State("ship");
        other["sequence"] = 30;
        other["gameplayRevision"] = 8;
        bench.Set("ship", other);
        Expect(!mount.Has("fleet"), "the ship of another gameplay revision is not shown");
        Json deck = State("gameplay-sailing");
        deck["revision"] = 11;
        deck["capabilities"].push_back("DeckCamera_Forward");
        deck["capabilities"].push_back("DeckCamera_Backward");
        bench.Set("gameplay", deck);
        Expect(mount.Has("walk"), "first person on deck: the walk stick");
        const size_t before_walk = mount.commands.size();
        bench.Press("walk", "change", "4,-5");
        Expect(mount.commands.size() == before_walk, "a thumb resting in the dead zone does not walk");
        bench.Press("walk", "change", "10,-70");
        Expect(mount.Last() == "deckwalk:11\t8\t53" && mount.slots.back() == "deckwalk",
               "up walks forward, a little to the right: " + mount.Last());
        bench.Press("walk", "change", "60,0");
        Expect(mount.Last() == "deckwalk:11\t40\t0", "to the side it strafes, as far as it is pushed: " + mount.Last());
        bench.Press("walk", "change", "-50,-50");
        Expect(mount.Last() == "deckwalk:11\t-38\t38", "forward and to the left: " + mount.Last());
        bench.Press("walk", "change", "0,80");
        Expect(mount.Last() == "deckwalk:11\t0\t-68", "down walks back: " + mount.Last());
        bench.Press("walk", "change", "0,0");
        Expect(mount.Last() == "deckwalk:11\t0\t0", "let go: " + mount.Last());
        bench.Press("walk", "change", "0,-100");
        bench.hud.Released();
        Expect(mount.Last() == "deckwalk:11\t0\t0", "cancelled input stops the walk: " + mount.Last());
        Expect(!Sent(mount, "hold:11\tDeckCamera_Forward\t1"), "the walk is no key any more");
        Json helmless = State("gameplay-sailing");
        helmless["revision"] = 12;
        helmless["capabilities"] = Json::array({"Ship_Fire"});
        const size_t errors = mount.errors.size();
        bench.Set("gameplay", helmless);
        Expect(!mount.Has("helm") && !mount.Has("camera") && mount.Has("fire") && mount.errors.size() == errors + 1,
               "a gameplay without a helm says so and shows none");
    }
};

static void CheckSailing()
{
    SailingRun run;
    run.Shown();
    run.FirstFrame();
    run.Shortcuts();
    run.Helm();
    run.SailLever();
    run.AmmunitionFan();
    run.Minimap();
    run.EventLines();
    run.Indicators();
    run.StatesTakeAway();
}

// Moor and the world map together: both buttons, each activates its own action, once.
static void CheckMoorAndMap()
{
    SeaBench bench;
    FakeMount &mount = bench.mount;
    Sail(bench);
    const Json &shown = mount.shown.elements;
    bench.Frame();
    bench.Feed("seaQuickAction", "sea-quick-moor-map");
    Expect(mount.Has("context") && mount.Has("map") && shown["map"]["icon"] == "map" &&
               shown["map"]["label"] == "Выйти на карту" && shown["context"]["icon"] == "moor",
           "moor with the map beside it: " + shown["map"].dump());
    bench.Press("map");
    Expect(!Sent(mount, "seaquick:32\tMap"), "the map button of an offer not drawn yet cannot be tapped");
    bench.Frame();
    bench.Press("map");
    Expect(mount.Last() == "seaquick:32\tMap", "the map button activates Map: " + mount.Last());
    bench.Press("context");
    Expect(Count(mount, "seaquick:32\tMoor") == 0, "one tap per offer: the engine answers with a new revision");
    Expect(mount.errors.empty(), "moor and map: no errors");
}

// A sea battle on one HUD: the layout, the ship abilities, one ability alone, the spyglass and a held helm.
struct BattleRun
{
    SeaBench bench;
    FakeMount &mount = bench.mount;
    const Json &shown = mount.shown.elements;

    void Layout()
    {
        bench.Feed("seaOrders", "sea-orders-idle");
        bench.Feed("gameplay", "gameplay-naval");
        bench.Feed("ship", "ship-battle");
        bench.Feed("seaQuickAction", "sea-quick-board");
        Expect(mount.shown.layout == "sea-battle" && mount.Has("spyglass") && shown["fleet"]["ships"].size() == 8,
               "battle: its layout, the spyglass on deck, the flagship and seven companions");
        Expect(shown["fire"]["fill"] == 65 && shown["fire"]["fillTone"] == "reload" &&
                   shown["fire"]["reloading"] == true,
               "guns that reload fill the fire button: " + shown["fire"].dump());
        Expect(shown["ammo"]["badge"] == "12" && shown["ammo"]["badgeTone"] == "danger" &&
                   shown["fleet"]["ships"][0]["bars"][0]["value"] == 27 && shown["sail"]["value"] == 1 &&
                   shown["minimap"]["date"] == "7 октября 1654 · 11:42",
               "battle values");
        Expect(shown["time"]["badge"] == "×1.5" && shown["time"]["variant"] == "active",
               "an accelerated world: " + shown["time"].dump());
        bench.Press("time");
        Expect(mount.Last() == "gameplay:10\tTimeScaleX4", "x1.5 counts as x2: the next step is x4: " + mount.Last());
    }

    void Abilities()
    {
        // The ship abilities: one small button that lays them out as HUD buttons beside it (a picker of the scene,
        // Fan).
        Expect(shown["abilities"]["icon"] == "rank" && shown["abilities"]["cue"] == true &&
                   !shown["abilities"].contains("badge") && shown["abilities"]["fill"] == 50 &&
                   shown["abilities"]["fillTone"] == "reload" && !mount.Has("abilities-1") && mount.shown.sheet.empty(),
               "collapsed abilities: a dot for the ready one, the fill of the running one: " +
                   shown["abilities"].dump());
        Expect(mount.Has("camera") && mount.Has("time"), "collapsed: the camera and time buttons are there");
        bench.Press("abilities");
        Expect(mount.Has("abilities-1") && mount.Has("abilities-2") && mount.Has("abilities-3") &&
                   mount.Has("abilities-4") && !mount.Has("abilities-5") && shown["abilities"]["variant"] == "active" &&
                   mount.shown.sheet.empty() && !mount.shown.modal && mount.shown.back,
               "a tap lays the four abilities out as HUD buttons, no sheet; Back is the HUD's: " +
                   shown["abilities"].dump());
        Expect(!mount.Has("camera") && !mount.Has("time") && mount.Has("fire") && mount.Has("helm") &&
                   mount.Has("ammo") && mount.Has("context"),
               "the row takes the place of the buttons under it (camera, time), the rest stays");
        Expect(shown["abilities-1"]["icon"] == "reload" &&
                   shown["abilities-1"]["label"] == "Предварительная перезарядка" &&
                   shown["abilities-1"]["disabled"] == true && shown["abilities-1"]["fill"] == 40 &&
                   shown["abilities-1"]["fillTone"] == "cooldown" && shown["abilities-1"]["badge"] == "180",
               "a recovering ability: greyed, its recovery and seconds: " + shown["abilities-1"].dump());
        Expect(shown["abilities-2"]["icon"] == "repair" && shown["abilities-2"]["disabled"] == true &&
                   !shown["abilities-2"].contains("badge") && !shown["abilities-2"].contains("reason"),
               "a blocked ability: greyed, no words: " + shown["abilities-2"].dump());
        Expect(shown["abilities-3"]["icon"] == "perk-instantrepair" &&
                   shown["abilities-3"]["label"] == "Ремонт в бою" && !shown["abilities-3"].contains("disabled"),
               "a ready ability under the game's own name: " + shown["abilities-3"].dump());
        Expect(shown["abilities-4"]["icon"] == "maneuver" && !shown["abilities-4"].contains("disabled") &&
                   shown["abilities-4"]["fill"] == 50 && shown["abilities-4"]["fillTone"] == "reload" &&
                   shown["abilities-4"]["badge"] == "30",
               "a running ability: its fill and seconds: " + shown["abilities-4"].dump());
        const size_t launched = mount.commands.size();
        const size_t errors_before = mount.errors.size();
        bench.Press("abilities-2", "refused");
        bench.Press("abilities-1");
        bench.Press("abilities-4");
        Expect(mount.commands.size() == launched && mount.Has("abilities-1") && mount.errors.size() == errors_before,
               "blocked, recovering and running abilities launch nothing, quietly; the row stays");
        bench.Press("abilities-3");
        Expect(mount.Last() == "gameplay:10\tBI_InstantRepair" && !mount.Has("abilities-1") && mount.Has("camera") &&
                   mount.Has("time") && !mount.shown.back,
               "a ready ability launches through its command and the row folds: " + mount.Last());
        bench.Press("abilities");
        bench.Press("abilities");
        Expect(!mount.Has("abilities-1") && mount.commands.size() == launched + 1,
               "a second tap on the button folds the row, launching nothing");
        bench.Press("abilities");
        bench.Frame(7.0);
        Expect(!mount.Has("abilities-1") && mount.Has("camera"), "an untouched row folds by itself");
        bench.Press("abilities");
        Expect(bench.hud.Back(), "Back is the open row's");
        bench.Frame();
        Expect(!mount.Has("abilities-1"), "Back folds the row");
        Expect(!bench.hud.Back(), "… and a second Back is not the HUD's");
        bench.Press("abilities");
        bench.Press("ammo");
        Expect(mount.Has("ammo-1") && !mount.Has("abilities") && !mount.Has("abilities-1") && mount.Has("time"),
               "the ammunition fan takes the place of the abilities and folds their row");
        bench.Press("ammo");
        Expect(!mount.Has("ammo-1") && mount.Has("abilities") && !mount.Has("abilities-1"),
               "folded: the abilities button is back, folded");
    }

    void OneAbility()
    {
        // one ability: still the button and a row of one; a command the gameplay does not bind blocks it
        Json single = State("ship-battle");
        single["sequence"] = 99;
        single["abilities"] = Json::array({Json{{"id", "Turn180"},
                                                {"label", "Манёвренный разворот корабля"},
                                                {"state", "ready"},
                                                {"fill", 0},
                                                {"left", 0}}});
        bench.Set("ship", single);
        Expect(shown["abilities"]["icon"] == "rank" && shown["abilities"]["cue"] == true,
               "one ability: still the abilities button: " + shown["abilities"].dump());
        bench.Press("abilities");
        Expect(mount.Has("abilities-1") && !mount.Has("abilities-2"), "… whose row has the one ability");
        bench.Press("abilities-1");
        Expect(mount.Last() == "gameplay:10\tBI_Turn180" && !mount.Has("abilities-1"),
               "… and a tap on it launches it: " + mount.Last());
        Json unbound = State("gameplay-naval");
        unbound["revision"] = 10;
        auto &caps = unbound["capabilities"];
        caps.erase(std::remove(caps.begin(), caps.end(), Json("BI_Turn180")), caps.end());
        bench.Set("gameplay", unbound);
        Expect(shown["abilities"]["cue"] == false,
               "an ability whose command the gameplay does not bind is blocked: no dot: " + shown["abilities"].dump());
        bench.Press("abilities");
        Expect(shown["abilities-1"]["disabled"] == true, "… and greyed in the row: " + shown["abilities-1"].dump());
        bench.Press("abilities");
        bench.Press("abilities");
        Json none = State("ship-battle");
        none["sequence"] = 100;
        none["abilities"] = Json::array();
        bench.Set("ship", none);
        Expect(!mount.Has("abilities") && !mount.Has("abilities-1") && mount.Has("camera") && mount.Has("time"),
               "no ability: no button at all, an open row goes with it");
        bench.Set("gameplay", State("gameplay-naval"));
        Json restored = State("ship-battle");
        restored["sequence"] = 101;
        bench.Set("ship", restored);
        Expect(!mount.Has("abilities-1"), "… and does not come back with the abilities");
    }

    void Spyglass()
    {
        // The spyglass is a toggle on the left, of the game's own telescope: a tap raises it (TelescopeIn), the next
        // tap or Back lowers it (TelescopeOut); up or down is the game's channel "spyglass".
        Expect(mount.Has("spyglass") && shown["spyglass"]["variant"] == "normal" && !mount.shown.back,
               "the spyglass, down: " + shown["spyglass"].dump());
        bench.Press("spyglass");
        Expect(mount.Last() == "gameplay:10\tTelescopeIn" && shown["spyglass"]["variant"] == "normal" &&
                   mount.Has("abilities") && mount.Has("fleet"),
               "a tap asks the game to raise the spyglass and the HUD waits for it: " + mount.Last());
        bench.Feed("spyglass", "spyglass-ship");
        Expect(shown["spyglass"]["variant"] == "active" && mount.shown.back,
               "raised by the game: shown raised; Back is the HUD's");
        Expect(!mount.Has("abilities") && !mount.Has("context") && !mount.Has("camera") && !mount.Has("pause") &&
                   !mount.Has("ship") && !mount.Has("fleet") && !mount.Has("minimap") && mount.Has("helm") &&
                   mount.Has("fire") && mount.Has("sail"),
               "the raised spyglass keeps the steering and the guns, the rest gives the look room");
        bench.Frame(kRaiseWait);
        Expect(shown["spyglass"]["variant"] == "active" && !Sent(mount, "gameplay:10\tTelescopeOut"),
               "raised by the game: it stays up, nothing held");
        bench.Press("spyglass");
        Expect(mount.Last() == "gameplay:10\tTelescopeOut" && shown["spyglass"]["variant"] == "active",
               "the next tap asks the game to lower it, shown up until the game lowers it: " + mount.Last());
        bench.Feed("spyglass", "spyglass-down");
        Expect(shown["spyglass"]["variant"] == "normal" && mount.Has("abilities"),
               "lowered by the game: the HUD is back");
        bench.Feed("spyglass", "spyglass-ship");
        Expect(shown["spyglass"]["variant"] == "active",
               "raised another way (the game's own key): the button follows the game");
        Expect(bench.hud.Back(), "Back is the raised spyglass's");
        bench.Frame();
        Expect(mount.Last() == "gameplay:10\tTelescopeOut", "Back lowers it: " + mount.Last());
        bench.Feed("spyglass", "spyglass-down");
        bench.Press("spyglass");
        bench.Frame(kRaiseWait);
        Expect(shown["spyglass"]["variant"] == "normal", "a spyglass the game never raises is shown down");
        Json deckless = State("gameplay-naval");
        deckless["revision"] = 10;
        auto &deck = deckless["capabilities"];
        deck.erase(std::remove(deck.begin(), deck.end(), Json("TelescopeIn")), deck.end());
        bench.Set("gameplay", deckless);
        Expect(!mount.Has("spyglass"), "off the deck (the game binds no telescope): no spyglass button");
        bench.Set("gameplay", State("gameplay-naval"));
    }

    void HeldHelm()
    {
        // a helm held through sailing ↔ battle keeps turning in the new gameplay revision
        bench.Press("helm", "change", "-100");
        Expect(mount.Last() == "hold:10\tShip_TurnLeft\t1\t100", "helm in battle: " + mount.Last());
        bench.Feed("gameplay", "gameplay-sailing");
        bench.Frame(0.12);
        Expect(mount.shown.layout == "sea" && !Sent(mount, "hold:10\tShip_TurnLeft\t0") &&
                   Count(mount, "hold:10\tShip_TurnLeft\t1\t100") == 1 && !Sent(mount, "hold:9\tShip_TurnLeft\t1\t100"),
               "the held helm carries on through a new gameplay revision");
    }
};

static void CheckBattle()
{
    BattleRun run;
    run.Layout();
    run.Abilities();
    run.OneAbility();
    run.Spyglass();
    run.HeldHelm();
}

// The loaded kind ran out (regression: no cannonballs, other kinds in the hold, no way to switch):
// its 0 stays a count on a live switch, and the fan offers every kind the engine binds, an empty one with
// a red 0, as the scripts switch to any kind (Ship_ChangeCharge) and refuse only the reload.
static void CheckEmptyAmmo()
{
    SeaBench bench;
    FakeMount &mount = bench.mount;
    bench.Feed("seaOrders", "sea-orders-idle");
    bench.Feed("gameplay", "gameplay-sailing");
    Json ship = State("ship");
    ship["ammoCount"] = 0;
    ship["ammoCounts"] = Json{{"balls", 0}, {"grapes", 50}, {"knippels", 0}, {"bombs", 40}};
    ship["navigatorAmmoWarning"] = true;
    bench.Set("ship", ship);
    const Json &shown = mount.shown.elements;
    Expect(shown["ammo"]["badge"] == "0" && shown["ammo"]["badgeTone"] == "danger" &&
               shown["ammo"]["countOnly"] == true && !shown["ammo"].contains("disabled"),
           "an empty loaded kind: a red 0 on a switch that stays live: " + shown["ammo"].dump());
    bench.Press("ammo");
    Expect(mount.Has("ammo-1") && mount.Has("ammo-2") && mount.Has("ammo-3"),
           "the fan opens over an empty loaded kind");
    Expect(shown["ammo-1"]["icon"] == "grape" && shown["ammo-1"]["badge"] == "50" &&
               shown["ammo-1"]["disabled"] == false && !shown["ammo-1"].contains("countOnly") &&
               shown["ammo-2"]["icon"] == "chain" && shown["ammo-2"]["badge"] == "0" &&
               shown["ammo-2"]["disabled"] == false && shown["ammo-2"]["countOnly"] == true &&
               shown["ammo-2"]["badgeTone"] == "danger" && shown["ammo-3"]["icon"] == "bomb" &&
               shown["ammo-3"]["badge"] == "40" && shown["ammo-3"]["disabled"] == false,
           "every bound kind is offered, an empty one with a red 0: " + shown["ammo-1"].dump() +
               shown["ammo-2"].dump() + shown["ammo-3"].dump());
    bench.Press("ammo-2");
    Expect(mount.Last() == "gameplay:9\thk_charge3" && !mount.Has("ammo-1"),
           "an empty kind is asked of the game like any other: " + mount.Last());
    bench.Press("ammo");
    bench.Press("ammo-1");
    Expect(mount.Last() == "gameplay:9\thk_charge2" && !mount.Has("ammo-1"), "grapes are loaded: " + mount.Last());

    // A kind whose control the engine does not bind is greyed and sends nothing.
    Json gameplay = State("gameplay-sailing");
    gameplay["revision"] = 10;
    Json &capabilities = gameplay["capabilities"];
    capabilities.erase(std::remove(capabilities.begin(), capabilities.end(), Json("hk_charge4")), capabilities.end());
    bench.Set("gameplay", gameplay);
    ship["gameplayRevision"] = 10;
    ship["sequence"] = 21;
    bench.Set("ship", ship);
    bench.Press("ammo");
    Expect(mount.Has("ammo-3") && shown["ammo-3"]["icon"] == "bomb" && shown["ammo-3"]["disabled"] == true,
           "an unbound kind is greyed: " + shown["ammo-3"].dump());
    const size_t sent = mount.commands.size();
    bench.Press("ammo-3");
    Expect(mount.commands.size() == sent, "an unbound kind sends nothing");
    Expect(mount.errors.empty(), "empty ammunition: no errors");
}

static void CheckOrders()
{
    SeaBench bench;
    FakeMount &mount = bench.mount;
    Sail(bench);
    bench.Feed("gameplay", "gameplay-naval");
    bench.Frame();
    bench.Press("helm", "change", "100");
    bench.Press("orders");
    Expect(Sent(mount, "hold:10\tShip_TurnRight\t0") && mount.Last() == "orders:14\topen\t",
           "opening the orders lets the helm go first");

    // the engine opens its menu and blocks gameplay: the panel stays on the last layout
    bench.Feed("seaOrders", "sea-orders-root");
    bench.Feed("gameplay", "gameplay-orders");
    bench.Feed("ship", "ship-orders");
    const Json &props = mount.shown.sheet_props;
    Expect(mount.visible && mount.shown.layout == "sea-battle" && mount.shown.sheet == "s-hud-orders" &&
               mount.shown.modal,
           "the orders panel is a modal sheet over the sea");
    Expect(mount.Has("fleet") && !mount.shown.elements["fleet"]["ships"][0].contains("tap") && !mount.Has("helm") &&
               !mount.Has("fire") && mount.Has("minimap") && mount.shown.elements["minimap"]["readout"] == false,
           "under the panel the squadron (not tappable) and the radar stay");
    Expect(props["title"] == "hud.orders.title" && props["back"] == false && props["crumb"] == "" &&
               props["recipients"].size() == 4 && props["tiles"].size() == 5 && props["rows"].size() == 1,
           "first stage: ships, order tiles, ability rows: " + props.dump());
    Expect(props["recipients"][0]["selected"] == true && props["recipients"][0]["meta"] == "hud.orders.flagship" &&
               props["recipients"][0]["value"] == "82%" && props["recipients"][1]["image"]["icon"] == "ship-brig" &&
               !props["recipients"][0].contains("bars"),
           "the flagship is chosen");
    Expect(props["tiles"][0]["image"]["icon"] == "sail-to" && props["tiles"][1]["image"]["icon"] == "map" &&
               props["tiles"][3]["image"]["icon"] == "cabin" && props["rows"][0]["disabled"] == true &&
               props["rows"][0]["cooldown"] == 60,
           "orders have their icons, a cooling ability its share");
    Expect(!Sent(mount, "orders:15\tshown\t1"), "a built panel is not acknowledged");
    bench.Frame();
    Expect(mount.Last() == "orders:15\tshown\t1" && mount.slots.back() == "orders-ack",
           "the drawn panel is acknowledged: " + mount.Last());

    // a tap acts at once: select, then activate against the revision that confirms the selection
    bench.Press("item-i1");
    Expect(mount.Last() == "orders:15\tselectItem\ti1" && mount.shown.sheet_props["tiles"][1]["pressed"] == true,
           "a tap selects: " + mount.Last());
    const size_t commandsBefore = mount.commands.size();
    bench.Press("item-i1");
    Expect(mount.Last() == "orders:15\tselectItem\ti1" && mount.commands.size() == commandsBefore,
           "the same tap again while it is on its way does nothing: " + mount.Last() + " " +
               std::to_string(mount.commands.size() - commandsBefore));
    bench.Press("item-i0");
    Expect(mount.Last() == "orders:15\tselectItem\ti0" && mount.shown.sheet_props["tiles"][0]["pressed"] == true &&
               mount.shown.sheet_props["tiles"][1]["pressed"] == false,
           "another tap before the engine answered replaces the first (it is never swallowed): " + mount.Last());
    bench.Press("item-i1");
    Expect(mount.Last() == "orders:15\tselectItem\ti1", "and back");
    bench.Feed("seaOrders", "sea-orders-root-selected");
    bench.Frame();
    Expect(Sent(mount, "orders:16\tshown\t1") && mount.Last() == "orders:16\tactivateItem\ti1",
           "… and activates once the engine selected it: " + mount.Last());
    bench.Press("item-i5");
    Expect(mount.Last() == "orders:16\tactivateItem\ti1", "a cooling ability cannot be chosen");
    // the activated order goes on to its targets: the commands' tiles go, the mark with them
    {
        Json targets = State("sea-orders-targets");
        targets["recipientId"] = "r501_1";
        bench.Set("seaOrders", targets);
        Expect(!mount.shown.sheet_props.contains("tiles") && mount.shown.sheet_props["rows"].size() == 3,
               "the order's targets replace the commands: " + mount.shown.sheet_props.dump());
        bench.Set("seaOrders", State("sea-orders-root-selected"));
    }

    // another ship; its bars under its row
    bench.Frame(2.0);
    bench.Press("recipient-r502_7");
    Expect(mount.Last() == "orders:16\tselectRecipient\tr502_7", "a tap on a ship: " + mount.Last());
    bench.Feed("seaOrders", "sea-orders-companion");
    Expect(mount.shown.sheet_props["recipients"][1]["selected"] == true &&
               mount.shown.sheet_props["recipients"][1]["bars"]["hull"] == 74 &&
               mount.shown.sheet_props["tiles"][2]["image"]["icon"] == "follow" &&
               mount.shown.sheet_props["rows"][0]["image"]["icon"] == "brander",
           "the companion's orders and bars");
    bench.Press("recipient-r502_7");
    Expect(mount.Last() == "orders:16\tselectRecipient\tr502_7", "the chosen ship is not chosen again");

    // nested stages: back; targets are rows
    bench.Feed("seaOrders", "sea-orders-targets");
    Expect(mount.shown.sheet_props["back"] == true && mount.shown.sheet_props["crumb"] == "Атаковать" &&
               mount.shown.sheet_props["tiles"].empty() && mount.shown.sheet_props["rows"].size() == 3 &&
               mount.shown.sheet_props["rows"][2]["image"]["icon"] == "fort",
           "targets: " + mount.shown.sheet_props.dump());
    const size_t before = mount.commands.size();
    bench.Press("recipient-r503_8");
    Expect(mount.commands.size() == before, "ships are not switched in the middle of an order");
    Expect(bench.hud.Back() && mount.Last() == "orders:18\tback\t", "the back key steps back: " + mount.Last());
    bench.Press("sheet-back");
    Expect(mount.Last() == "orders:18\tback\t", "the ribbon steps back");
    bench.Feed("seaOrders", "sea-orders-options");
    Expect(mount.shown.sheet_props["tiles"].size() == 4 &&
               mount.shown.sheet_props["tiles"][1]["image"]["icon"] == "grape" &&
               mount.shown.sheet_props["tiles"][3]["disabled"] == true,
           "options: the charges with the ammunition icons");
    bench.Press("sheet", "close");
    Expect(mount.Last() == "orders:19\tclose\t", "a tap on the scrim closes: " + mount.Last());

    // a menu only the engine can draw stays the engine's
    const size_t errors = mount.errors.size();
    bench.Feed("seaOrders", "sea-orders-unknown-kind");
    bench.Frame();
    Expect(mount.shown.sheet.empty() && !mount.shown.modal && mount.errors.size() == errors + 1 &&
               !Sent(mount, "orders:20\tshown\t1") && !bench.hud.Back(),
           "an undrawable menu: an error, no panel, no acknowledgement");

    // the game loses the screen with the panel open
    Json again = State("sea-orders-root");
    again["revision"] = 21;
    bench.Set("seaOrders", again);
    bench.Frame();
    Expect(mount.Last() == "orders:21\tshown\t1", "drawn again");
    mount.commands.clear();
    bench.Feed("presentation", "presentation-background");
    Expect(Sent(mount, "orders:21\tshown\t0") && mount.Last() == "orders:21\tclose\t",
           "not live: the coverage is withdrawn and the menu closed");

    // the menu closes: the sea HUD is back
    bench.Feed("presentation", "presentation-game");
    Json idle = State("sea-orders-idle");
    idle["revision"] = 22;
    bench.Set("seaOrders", idle);
    bench.Feed("gameplay", "gameplay-sailing");
    bench.Feed("ship", "ship");
    Expect(mount.shown.sheet.empty() && !mount.shown.modal && mount.Has("helm") && mount.Has("orders") &&
               mount.shown.layout == "sea",
           "the panel closed");
}

// Selection and activation are separate replies. A recipient switch must cancel the
// pending tap, even if the next ship reuses the same item id and menu stage.
// A tapped order is drawn until the game answers it, not for a time: unanswered it holds, refused it ends at
// once, applied it ends with the menu revision that shows it.
static void CheckOrderAnswers()
{
    SeaBench bench;
    FakeMount &mount = bench.mount;
    Sail(bench);
    bench.Feed("seaOrders", "sea-orders-root");
    bench.Feed("gameplay", "gameplay-orders");
    bench.Feed("ship", "ship-orders");
    bench.Frame();
    const auto pressed = [&] { return mount.shown.sheet_props["tiles"][1]["pressed"] == true; };
    bench.Press("item-i1");
    bench.Frame(2.0);
    Expect(pressed(), "unanswered, a tapped order stays pressed however long the game takes");
    bench.Answer(false);
    Expect(!pressed(), "a refusal ends it at once");
    bench.Press("item-i1");
    Expect(mount.Last() == "orders:15\tselectItem\ti1" && pressed(), "tapped again: " + mount.Last());
    bench.Answer(true, 16);
    Expect(pressed(), "applied: pressed until the menu revision that shows it arrives");
    bench.Feed("seaOrders", "sea-orders-root-selected");
    bench.Frame();
    Expect(mount.Last() == "orders:16\tactivateItem\ti1" && pressed(),
           "the revision shows it selected: it is activated, still pressed: " + mount.Last());
    const double now = mount.now;
    bench.Answer(true, 16);
    Expect(!pressed() && mount.now == now, "activated and shown by the menu as drawn: it ends, with no clock");
}

static void CheckOrderRecipientChange()
{
    for (const bool explicit_switch : {false, true})
    {
        SeaBench bench;
        FakeMount &mount = bench.mount;
        Sail(bench);
        bench.Feed("seaOrders", "sea-orders-root");
        bench.Feed("gameplay", "gameplay-orders");
        bench.Feed("ship", "ship-orders");
        bench.Frame();
        bench.Press("item-i1");
        Expect(mount.Last().find("\tselectItem\ti1") != std::string::npos, "pending order selects before activation");
        if (explicit_switch)
            bench.Press("recipient-r502_7");
        mount.commands.clear();
        Json reply = State("sea-orders-root-selected");
        if (!explicit_switch)
            reply["recipientId"] = "r502_7";
        // Explicit switch: even a delayed old-recipient selection reply must not
        // finish the cancelled action while selectRecipient is in flight.
        bench.Set("seaOrders", reply);
        bench.Frame();
        bool action = false;
        for (const auto &command : mount.commands)
            action |= command.find("\tselectItem\t") != std::string::npos ||
                      command.find("\tactivateItem\t") != std::string::npos;
        Expect(!action, explicit_switch ? "recipient tap cancels pending order before delayed reply"
                                        : "recipient replacement cannot inherit pending item activation");
        Expect(mount.shown.sheet_props["tiles"][1]["pressed"] == false, "cancelled recipient order is not pressed");
        mount.commands.clear();
        bench.Press("item-i1");
        Expect(mount.Last().find("\tactivateItem\ti1") != std::string::npos,
               "a fresh tap in the current recipient still acts normally");
        Expect(mount.errors.empty(), "recipient change controls have no channel errors");
    }
}

static void CheckOrderNavigationCancel()
{
    for (const std::string route : {"root-back", "nested-back", "ribbon-back", "close", "scrim"})
    {
        SeaBench bench;
        FakeMount &mount = bench.mount;
        Sail(bench);
        Json orders = State(route == "root-back" ? "sea-orders-root" : "sea-orders-targets");
        bench.Set("seaOrders", orders);
        bench.Feed("gameplay", "gameplay-orders");
        bench.Feed("ship", "ship-orders");
        bench.Frame();
        bench.Press("item-i1");
        Expect(mount.Last().find("\tselectItem\ti1") != std::string::npos,
               route + ": selection waits for acknowledgement");
        if (route == "root-back" || route == "nested-back")
            Expect(bench.hud.Back(), route + ": Back handled");
        else if (route == "ribbon-back")
            bench.Press("sheet-back");
        else if (route == "close")
            bench.Press("sheet-close");
        else
            bench.Press("sheet", "close");
        const std::string verb = route == "nested-back" || route == "ribbon-back" ? "back" : "close";
        Expect(mount.Last().find("\t" + verb + "\t") != std::string::npos, route + ": navigation sent");
        mount.commands.clear();
        // The selection reply was already in flight before navigation. It still
        // describes the same panel, with a new revision and the selected item.
        orders["revision"] = orders["revision"].get<int>() + 1;
        orders["selectedItemId"] = "i1";
        bench.Set("seaOrders", orders);
        bench.Frame();
        bool action = false;
        for (const auto &command : mount.commands)
            action |= command.find("\tselectItem\t") != std::string::npos ||
                      command.find("\tactivateItem\t") != std::string::npos;
        Expect(!action, route + ": delayed selection reply cannot activate cancelled intent");
        const auto &entries = mount.shown.sheet_props[route == "root-back" ? "tiles" : "rows"];
        Expect(entries[1]["pressed"] == false, route + ": cancelled item loses its pressed state");
        mount.commands.clear();
        bench.Press("item-i1");
        Expect(mount.Last().find("\tactivateItem\ti1") != std::string::npos, route + ": a fresh tap remains usable");
        Expect(mount.errors.empty(), route + ": no channel errors");
    }
}

static void CheckEmptyCategory()
{
    SeaBench bench;
    FakeMount &mount = bench.mount;
    Sail(bench);
    bench.Feed("seaOrders", "sea-orders-root");
    bench.Feed("gameplay", "gameplay-orders");
    bench.Feed("ship", "ship-orders");
    bench.Frame();
    // a command whose target list would be empty (the engine counts it with the list's own
    // builder) is a disabled tile without a cooldown fill, and a tap on it does nothing
    {
        Json empty = State("sea-orders-root-selected");
        empty["revision"] = 16;
        empty["selectedItemId"] = "";
        empty["items"][2]["enabled"] = false; // Выслать шлюпку, nobody to send the boat to
        empty["items"][2]["cooldown"] = 1.0;
        bench.Set("seaOrders", empty);
        const Json &tile = mount.shown.sheet_props["tiles"][2];
        Expect(tile["disabled"] == true && tile["cooldown"] == 0, "an empty category is disabled: " + tile.dump());
        bench.Frame(2.0);
        const size_t before = mount.commands.size();
        bench.Press("item-i2");
        Expect(mount.commands.size() == before || mount.Last().find("\tack\t") != std::string::npos,
               "a tap on it sends nothing");
    }
}

// Where the helm's capsule is drawn in the runtime (its left edge and middle height, dp = px here).
static Rml::Vector2f HelmAt()
{
    Rml::Context *context = Rml::GetContext("storm-ui");
    for (int i = 0; context && i < context->GetNumDocuments(); ++i)
        if (Rml::Element *helm = context->GetDocument(i)->GetElementById("helm"))
            if (Rml::Element *capsule = helm->QuerySelector("[data-stick]"))
                return {capsule->GetAbsoluteLeft(),
                        capsule->GetAbsoluteTop() + capsule->GetBox().GetSize(Rml::BoxArea::Border).y * 0.5f};
    return {-1.0f, -1.0f};
}

// The centre of the element `selector` finds in the HUD (the application pack's layout, 800 × 400 dp).
static Rml::Vector2f Centre(const char *selector)
{
    Rml::Context *context = Rml::GetContext("storm-ui");
    for (int i = 0; context && i < context->GetNumDocuments(); ++i)
        if (Rml::Element *element = context->GetDocument(i)->QuerySelector(selector))
        {
            const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
            return {element->GetAbsoluteLeft() + size.x * 0.5f, element->GetAbsoluteTop() + size.y * 0.5f};
        }
    return {-1.0f, -1.0f};
}

// The sea HUD's touches on one runtime: the layout, input going off, the helm and the guns, the sail lever, the helm
// through a battle, taps and the modal orders panel.
struct SeaTouches
{
    Runtime &run;
    Host &host;

    void Layout()
    {
        run.Feed("presentation", "presentation-game");
        run.Feed("seaOrders", "sea-orders-idle");
        run.Feed("gameplay", "gameplay-sailing");
        run.Feed("ship", "ship");
        run.Feed("seaQuickAction", "sea-quick-moor");
        run.Frame();
        run.Frame();

        // The test layout (800 × 400): the helm zone 0…280 × 176…400 (under the squadron) with the
        // helm resting at 20…220 × 309…373, fire 708…784 × 308…384, minimap disc 654…754 × 34…134,
        // the sail lever 732…788 × 154…298 (its knob's centre at 178 full, 226 half, 274 furled),
        // context 640…704 × 226…290, camera 536…584 × 336…384, the first flagship 10…62 × 10…62,
        // then a companion every 60 dp down.
        Expect(host.WantsTouch(120, 341) && host.WantsTouch(746, 346) && host.WantsTouch(704, 84) &&
                   host.WantsTouch(760, 226) && host.WantsTouch(672, 258) && host.WantsTouch(36, 86),
               "helm, fire, minimap, lever, quick action and companions are the UI's");
        Expect(host.WantsTouch(260, 390) && host.WantsTouch(4, 180) && host.WantsTouch(150, 200),
               "the whole helm zone is the UI's, high above the helm too");
        Expect(!host.WantsTouch(400, 200) && !host.WantsTouch(200, 30) && !host.WantsTouch(300, 341) &&
                   !host.WantsTouch(700, 150) && !host.WantsTouch(140, 170) && !host.WantsTouch(760, 140) &&
                   !host.WantsTouch(724, 226) && !host.WantsTouch(796, 226),
               "the open sea beside the HUD's controls is the game's");
        Expect(run.Sent("navigator:9\t3\t4\tprepare\t1\t800\t400\t704\t84\t50\t1000\t1"),
               "the aperture is the disc as it is laid out");

        Expect(HelmAt() == Rml::Vector2f(20.0f, 341.0f), "the helm rests at its place");
    }

    void InputOff()
    {
        // the input goes off: a press it cancelled, and a release whose press was dropped with it, fire nothing
        const size_t before_cancel = run.commands.size();
        host.Touch(TouchPhase::Down, 2, 746, 346);
        host.CancelInput();
        host.Touch(TouchPhase::Up, 2, 746, 346);
        host.Touch(TouchPhase::Up, 3, 746, 346);
        run.Frame();
        Expect(std::find(run.commands.begin() + before_cancel, run.commands.end(), "gameplay:9\tShip_Fire") ==
                   run.commands.end(),
               "a tap cut by the input going off does not fire");
    }

    void HelmGunsAndSails()
    {
        // the helm under one finger, the camera free, the guns under another; the helm comes to the
        // height of the finger that lands anywhere in its zone and keeps its left; across, it is
        // absolute: the rudder is where the finger is from the helm's middle (x 120, 66 dp of reach),
        // from the first contact on
        const size_t before_helm = run.commands.size();
        host.Touch(TouchPhase::Down, 1, 120, 385);
        Expect(run.commands.size() == before_helm, "a thumb that lands on the helm's middle turns nothing");
        run.Frame();
        Expect(HelmAt() == Rml::Vector2f(20.0f, 341.0f),
               "a thumb below the resting helm takes it where it rests, never lower: " + std::to_string(HelmAt().x) +
                   "," + std::to_string(HelmAt().y));
        host.Touch(TouchPhase::Move, 1, 150, 385);
        const std::string half_right = run.Last();
        Expect(half_right.rfind("hold:9\tShip_TurnRight\t1\t", 0) == 0,
               "dragged right of the middle, the helm turns right: " + half_right);
        host.Touch(TouchPhase::Move, 1, 180, 380);
        Expect(run.Last() == "hold:9\tShip_TurnRight\t1\t85",
               "… further right, harder, without a jump back to the middle: " + run.Last());
        Expect(std::stoi(half_right.substr(half_right.rfind('\t') + 1)) < 85,
               "… half as far turns less: " + half_right);
        host.Touch(TouchPhase::Up, 1, 180, 380);
        Expect(run.Last() == "hold:9\tShip_TurnRight\t0", "let go, the rudder returns: " + run.Last());
        run.Frame();
        host.Touch(TouchPhase::Down, 1, 180, 385); // right of the middle at once
        Expect(run.Last() == "hold:9\tShip_TurnRight\t1\t85",
               "a thumb that lands right of the helm's middle turns right at once, as far as it lands: " + run.Last());
        host.Touch(TouchPhase::Up, 1, 180, 385);
        run.Frame();
        host.Touch(TouchPhase::Down, 1, 250, 385); // the far right of the zone
        const size_t held_from = run.commands.size();
        Expect(run.Last() == "hold:9\tShip_TurnRight\t1\t100",
               "a thumb on the far right of the zone: full rudder at once: " + run.Last());
        run.Frame();
        Expect(HelmAt() == Rml::Vector2f(20.0f, 341.0f), "… the helm keeps its left");
        Expect(!host.WantsTouch(400, 200), "a held helm leaves the sea to the camera finger");
        host.Touch(TouchPhase::Down, 2, 746, 346);
        host.Touch(TouchPhase::Up, 2, 746, 346);
        Expect(run.Last() == "gameplay:9\tShip_Fire", "a second finger fires: " + run.Last());
        run.Frame(3.0);
        Expect(run.Last() == "gameplay:9\tShip_Fire" && std::find(run.commands.begin() + held_from, run.commands.end(),
                                                                  "hold:9\tShip_TurnRight\t0") == run.commands.end(),
               "the helm stays held through a slow frame, without a heartbeat: " + run.Last());
        // the sail lever stays where it is drawn: a finger takes the position it lands on, and a drag
        // moves a position every 48 dp, past half of it
        const size_t before_sails = run.commands.size();
        host.Touch(TouchPhase::Down, 2, 760, 274);
        Expect(run.Last() == "sails:9\t0", "a finger on the furled position furls the sails: " + run.Last());
        host.Touch(TouchPhase::Move, 2, 760, 255);
        Expect(run.commands.size() == before_sails + 1, "… less than half a step up changes nothing: " + run.Last());
        host.Touch(TouchPhase::Move, 2, 760, 249);
        Expect(run.Last() == "sails:9\t1", "… past half a step: half sails: " + run.Last());
        host.Touch(TouchPhase::Move, 2, 762, 150);
        Expect(run.Last() == "sails:9\t2", "… and up past the top: full sails: " + run.Last());
        host.Touch(TouchPhase::Move, 2, 762, 230);
        Expect(run.Last() == "sails:9\t1", "… back down: half sails: " + run.Last());
        const size_t sails_set = run.commands.size();
        host.Touch(TouchPhase::Up, 2, 762, 230);
        Expect(run.commands.size() == sails_set, "letting go of the lever keeps the sails as set");
        run.Frame();
        host.Touch(TouchPhase::Down, 2, 760, 180);
        Expect(run.Last() == "sails:9\t2", "a finger on the full sails' place sets them: " + run.Last());
        host.Touch(TouchPhase::Up, 2, 760, 180);
        host.Touch(TouchPhase::Move, 1, 30, 341);
        Expect(std::find(run.commands.begin() + held_from, run.commands.end(), "hold:9\tShip_TurnRight\t0") !=
                       run.commands.end() &&
                   run.Last() == "hold:9\tShip_TurnLeft\t1\t100",
               "the helm goes over: " + run.Last());
    }

    void HelmThroughBattle()
    {
        // the helm survives sailing → battle (the element is patched, not remounted)
        run.Feed("gameplay", "gameplay-naval");
        run.Feed("ship", "ship-battle");
        run.Frame(0.12);
        Expect(!run.Sent("hold:9\tShip_TurnLeft\t0"), "the held helm survives the change of layout");
        host.Touch(TouchPhase::Up, 1, 30, 341);
        Expect(run.Last() == "hold:10\tShip_TurnLeft\t0", "letting go straightens the helm: " + run.Last());
        run.Frame();
        host.Touch(TouchPhase::Down, 1, 60, 341); // on the resting helm, left of its middle
        Expect(run.Last() == "hold:10\tShip_TurnLeft\t1\t85",
               "a thumb left of the helm's middle turns left at once: " + run.Last());
        host.Touch(TouchPhase::Up, 1, 60, 341);
        Expect(run.Last() == "hold:10\tShip_TurnLeft\t0", "… until it lifts: " + run.Last());
        run.Frame();
        host.Touch(TouchPhase::Down, 1, 180, 190); // high in the zone, under the squadron's fourth ship
        Expect(run.Last() == "hold:10\tShip_TurnRight\t1\t85",
               "a thumb high in the zone steers by its offset from the helm's middle too: " + run.Last());
        run.Frame();
        Expect(HelmAt() == Rml::Vector2f(20.0f, 190.0f), "… and takes the helm up there, at its own left");
        host.Touch(TouchPhase::Move, 1, 120, 190);
        Expect(run.Last() == "hold:10\tShip_TurnRight\t0", "… dragged back to the middle, amidships: " + run.Last());
        host.Touch(TouchPhase::Up, 1, 120, 190);
        run.Frame();
        Expect(HelmAt() == Rml::Vector2f(20.0f, 341.0f), "let go, the helm returns to its place");
    }

    void Taps()
    {
        // taps: minimap, quick action, companions
        run.Feed("seaQuickAction", "sea-quick-board");
        run.Frame();
        host.Touch(TouchPhase::Down, 1, 704, 84);
        host.Touch(TouchPhase::Up, 1, 704, 84);
        Expect(run.Last() == "navigator:10\t3\t4\tin", "a tap on the disc zooms: " + run.Last());
        host.Touch(TouchPhase::Down, 1, 672, 258);
        host.Touch(TouchPhase::Up, 1, 672, 258);
        Expect(run.Last() == "seaquick:32\tBoard", "a tap on the quick action: " + run.Last());
        host.Touch(TouchPhase::Down, 1, 672, 258);
        host.Touch(TouchPhase::Up, 1, 672, 258);
        Expect(run.Last() == "seaquick:32\tBoard" && !run.Sent("seaquick:31\tMoor"), "one activation per offer");
        host.Touch(TouchPhase::Down, 1, 36, 146);
        host.Touch(TouchPhase::Up, 1, 36, 146);
        Expect(run.Last() == "orders:14\topen\tr503_8", "a tap on the second companion: " + run.Last());

        // the zones keep clear of the buttons beside them: a tap just inside a neighbour is the neighbour's
        run.Frame();
        const size_t before_taps = run.commands.size();
        host.Touch(TouchPhase::Down, 1, 538, 380); // the camera, right of the helm zone
        host.Touch(TouchPhase::Up, 1, 538, 380);
        Expect(run.commands.size() == before_taps + 1 && run.Last() == "gameplay:10\tSea_CameraSwitch",
               "the camera beside the helm zone: " + run.Last());
        host.Touch(TouchPhase::Down, 1, 712, 310); // the guns, under the lever
        host.Touch(TouchPhase::Up, 1, 712, 310);
        Expect(run.Last() == "gameplay:10\tShip_Fire", "the guns under the lever: " + run.Last());
        Expect(!host.WantsTouch(710, 230) && !host.WantsTouch(290, 380),
               "between the quick action and the lever, right of the helm zone: the game's");
        const Rml::Vector2f fourth = Centre("#fleet .item:nth-child(4)"); // the fourth ship, beside the helm zone
        host.Touch(TouchPhase::Down, 1, fourth.x, fourth.y);
        host.Touch(TouchPhase::Up, 1, fourth.x, fourth.y);
        Expect(run.Last().rfind("orders:", 0) == 0, "a ship of the squadron is tapped: " + run.Last());
        host.Touch(TouchPhase::Down, 1, 750, 130); // the minimap, above the lever
        host.Touch(TouchPhase::Up, 1, 750, 130);
        Expect(run.Last().rfind("navigator:10\t", 0) == 0, "the minimap above the lever: " + run.Last());
    }

    void ModalOrders()
    {
        // the orders panel is modal
        run.Feed("seaOrders", "sea-orders-root");
        run.Feed("gameplay", "gameplay-orders");
        run.Feed("ship", "ship-orders");
        run.Frame();
        run.Frame();
        Expect(host.WantsTouch(400, 200) && host.WantsTouch(5, 5) && host.WantsBack() &&
                   run.Sent("orders:15\tshown\t1"),
               "the open panel takes every touch and the back key");
        const Rml::Vector2f tile = Centre("#item-i1"); // the second tile
        host.Touch(TouchPhase::Down, 1, tile.x, tile.y);
        host.Touch(TouchPhase::Up, 1, tile.x, tile.y);
        Expect(run.Last() == "orders:15\tselectItem\ti1", "a tap on an order: " + run.Last());
        host.Back();
        Expect(run.Last() == "orders:15\tclose\t", "the back key closes the first stage: " + run.Last());
        host.Touch(TouchPhase::Down, 1, 400, 300); // beside everything
        host.Touch(TouchPhase::Up, 1, 400, 300);
        Expect(run.Last() == "orders:15\tclose\t", "a tap on the scrim closes");
        // the order goes on to its targets: the panel drops the commands' tiles it no longer gives
        Json targets = State("sea-orders-targets");
        targets["revision"] = 16;
        targets["recipientId"] = "r501_1";
        run.host->SetState("seaOrders", targets.dump());
        run.Frame();
        Expect(Centre(".ho-tile").x < 0.0f && Centre("#item-i2").x > 0.0f, "the targets replace the order tiles");
    }
};

static void CheckTouches(const std::string &pack)
{
    Runtime run(pack);
    AttachScripts(run);
    if (!run.host)
        return;
    SeaTouches touches{run, *run.host};
    touches.Layout();
    touches.InputOff();
    touches.HelmGunsAndSails();
    touches.HelmThroughBattle();
    touches.Taps();
    touches.ModalOrders();
}

// First person on deck: the walk stick floats in the whole left part, above and beside the helm;
// the helm and the squadron above keep their own touches (the zone lies under them). The helm
// stays where it rests there: its zone is off.
static void CheckDeckTouches(const std::string &pack)
{
    Runtime run(pack);
    AttachScripts(run);
    if (!run.host)
        return;
    Host &host = *run.host;
    run.Feed("presentation", "presentation-game");
    run.Feed("seaOrders", "sea-orders-idle");
    Json deck = State("gameplay-sailing");
    deck["capabilities"].push_back("DeckCamera_Forward");
    deck["capabilities"].push_back("DeckCamera_Backward");
    Expect(host.SetState("gameplay", deck.dump()), "the deck gameplay is accepted");
    run.Feed("ship", "ship");
    run.Frame();
    run.Frame();
    // The test layout (800 × 400): the walk zone 0…472 × 176…400 under the helm 20…220 × 309…373
    // and the squadron 10…62 × 10…, a ship every 60 dp down.
    Expect(host.WantsTouch(150, 250) && host.WantsTouch(120, 300) && host.WantsTouch(400, 380),
           "above and beside the helm is the walk stick's");
    Expect(!host.WantsTouch(150, 150) && !host.WantsTouch(600, 250), "above the zone and right of it the camera looks");
    host.Touch(TouchPhase::Down, 1, 150, 250);
    host.Touch(TouchPhase::Move, 1, 150, 200);
    Expect(run.Last() == "deckwalk:9\t0\t100", "the thumb above the helm walks forward: " + run.Last());
    host.Touch(TouchPhase::Move, 1, 200, 250);
    Expect(run.Last() == "deckwalk:9\t100\t0", "… and strafes to the right: " + run.Last());
    host.Touch(TouchPhase::Down, 2, 120, 341);
    host.Touch(TouchPhase::Move, 2, 180, 341);
    Expect(run.Last() == "hold:9\tShip_TurnRight\t1\t85",
           "the helm keeps its own zone, beside the walking thumb: " + run.Last());
    host.Touch(TouchPhase::Up, 2, 180, 341);
    host.Touch(TouchPhase::Up, 1, 200, 250);
    Expect(run.Last() == "deckwalk:9\t0\t0" && run.Sent("hold:9\tShip_TurnRight\t0"), "both let go: " + run.Last());
    const Rml::Vector2f fourth = Centre("#fleet .item:nth-child(4)"); // the fourth ship, above the walk zone
    host.Touch(TouchPhase::Down, 1, fourth.x, fourth.y);
    host.Touch(TouchPhase::Up, 1, fourth.x, fourth.y);
    Expect(run.Last().rfind("orders:", 0) == 0, "a ship of the squadron over the zone is still tapped: " + run.Last());

    // the helm stays where it rests: a finger on it turns as far as it lands from the centre,
    // and beside it, in what is the helm zone at sea, the walk stick takes the thumb
    run.Frame();
    Expect(host.WantsTouch(250, 385), "beside the resting helm is the walk zone");
    host.Touch(TouchPhase::Down, 1, 60, 341);
    Expect(run.Last() == "hold:9\tShip_TurnLeft\t1\t85",
           "the fixed helm turns where the finger lands on it: " + run.Last());
    host.Touch(TouchPhase::Up, 1, 60, 341);
    run.Frame();
    host.Touch(TouchPhase::Down, 1, 180, 341);
    Expect(run.Last() == "hold:9\tShip_TurnRight\t1\t85", "… right of its middle, right at once: " + run.Last());
    host.Touch(TouchPhase::Up, 1, 180, 341);
    run.Frame();
    const size_t before_middle = run.commands.size();
    host.Touch(TouchPhase::Down, 1, 120, 341);
    Expect(run.commands.size() == before_middle, "… on its middle, nothing: " + run.Last());
    host.Touch(TouchPhase::Up, 1, 120, 341);
    run.Frame();
    host.Touch(TouchPhase::Down, 1, 250, 385);
    host.Touch(TouchPhase::Move, 1, 250, 335);
    Expect(run.Last() == "deckwalk:9\t0\t100", "a thumb beside the helm walks: " + run.Last());
    host.Touch(TouchPhase::Up, 1, 250, 335);
    // the sail lever, as anywhere: a finger takes the position it lands on, and beside it is the camera's
    run.Frame();
    Expect(!host.WantsTouch(722, 200) && host.WantsTouch(760, 200), "beside the resting lever the camera looks");
    host.Touch(TouchPhase::Down, 2, 760, 280);
    Expect(run.Last() == "sails:9\t0", "the fixed lever takes the position under the finger: " + run.Last());
    host.Touch(TouchPhase::Up, 2, 760, 280);
}

// The deck camera switched on and off (the eye button) while a finger holds the helm high in its
// zone: on deck the helm goes back where it rests at once and the walk stick takes the zone; the
// held helm keeps turning until its finger lifts, and nothing stays held or out of place after.
static void CheckCameraSwitch(const std::string &pack)
{
    Runtime run(pack);
    AttachScripts(run);
    if (!run.host)
        return;
    Host &host = *run.host;
    run.Feed("presentation", "presentation-game");
    run.Feed("seaOrders", "sea-orders-idle");
    run.Feed("gameplay", "gameplay-sailing");
    run.Feed("ship", "ship");
    run.Frame();
    run.Frame();
    Json deck = State("gameplay-sailing");
    deck["capabilities"].push_back("DeckCamera_Forward");
    deck["capabilities"].push_back("DeckCamera_Backward");
    Json sailing = State("gameplay-sailing");

    host.Touch(TouchPhase::Down, 1, 150, 200); // high in the helm zone
    host.Touch(TouchPhase::Move, 1, 180, 200);
    run.Frame();
    Expect(HelmAt() == Rml::Vector2f(20.0f, 200.0f) && run.Last() == "hold:9\tShip_TurnRight\t1\t85",
           "the helm held high: " + run.Last());
    deck["revision"] = 10;
    Expect(host.SetState("gameplay", deck.dump()), "the deck camera is accepted");
    run.Frame();
    Expect(HelmAt() == Rml::Vector2f(20.0f, 341.0f), "on deck the held helm is drawn where it rests at once");
    Expect(host.WantsTouch(150, 250) && host.WantsTouch(250, 385),
           "on deck the rest of the helm zone is the walk stick's");
    host.Touch(TouchPhase::Down, 2, 250, 385);
    host.Touch(TouchPhase::Move, 2, 250, 335);
    Expect(run.Last() == "deckwalk:10\t0\t100", "a second thumb walks beside the held helm: " + run.Last());
    host.Touch(TouchPhase::Up, 2, 250, 335);
    host.Touch(TouchPhase::Up, 1, 180, 200);
    Expect(run.Sent("hold:10\tShip_TurnRight\t0") || run.Sent("hold:9\tShip_TurnRight\t0"),
           "lifting the finger lets the helm go");
    run.Frame();
    for (int round = 0; round < 3; ++round) // the eye button again and again
    {
        sailing["revision"] = 11 + 2 * round;
        Expect(host.SetState("gameplay", sailing.dump()), "back to the ship's camera");
        run.Frame();
        Expect(HelmAt() == Rml::Vector2f(20.0f, 341.0f) && !host.WantsTouch(400, 200),
               "the helm at rest, the sea free");
        host.Touch(TouchPhase::Down, 1, 100, 210);
        run.Frame();
        Expect(HelmAt() == Rml::Vector2f(20.0f, 210.0f), "the helm floats up to the thumb again");
        host.Touch(TouchPhase::Up, 1, 100, 210);
        deck["revision"] = 12 + 2 * round;
        Expect(host.SetState("gameplay", deck.dump()), "the deck camera again");
        run.Frame();
        Expect(HelmAt() == Rml::Vector2f(20.0f, 341.0f), "on deck the helm rests in its place");
        host.Touch(TouchPhase::Down, 1, 100, 210); // where the helm floated at sea: the walk stick's now
        host.Touch(TouchPhase::Move, 1, 100, 160);
        run.Frame();
        Expect(HelmAt() == Rml::Vector2f(20.0f, 341.0f) && run.Last().rfind("deckwalk:", 0) == 0,
               "on deck the thumb walks, the helm stays: " + run.Last());
        host.Touch(TouchPhase::Up, 1, 100, 160);
        Expect(run.Last().rfind("deckwalk:", 0) == 0 && run.Last().find("\t0\t0") != std::string::npos,
               "the walk stops: " + run.Last());
    }
}

// What a state change costs the scene (the host Release build is the measure): the ship publishes up to ten times a
// second while sailing; its speed, the date and the guns' reload only move readings, a hit asks the script for a build.
static void PrintCost()
{
    SeaBench bench;
    Sail(bench);
    const auto cost = [&](auto change) {
        Json state = State("ship");
        constexpr int kRounds = 200;
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kRounds; ++i)
        {
            change(state, i);
            state["sequence"] = 1000 + i;
            bench.Set("ship", state);
        }
        return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / kRounds;
    };
    const double readings = cost([](Json &state, int i) {
        state["shipSpeedKnots"] = 4.0 + (i % 50) * 0.1;
        state["cannonCharge"] = (i % 50) * 0.01;
    });
    const double builds = cost([](Json &state, int i) { state["hull"] = 2400 - i; });
    std::printf("sea scene per state change: %.0f us with readings only, %.0f us with a script build\n", readings,
                builds);
}

int main(int argc, char **argv)
{
    states_directory = argc > 1 ? argv[1] : "tests/states";
    const std::string pack = argc > 2 ? argv[2] : "build/app-pack";
    const ScriptPack scripts(pack);
    app_pack = &scripts;
    CheckConsumers();
    CheckSailing();
    CheckMoorAndMap();
    CheckBattle();
    CheckEmptyAmmo();
    CheckOrders();
    CheckOrderAnswers();
    CheckOrderRecipientChange();
    CheckOrderNavigationCancel();
    CheckEmptyCategory();
    CheckTouches(pack);
    CheckDeckTouches(pack);
    CheckCameraSwitch(pack);
    PrintCost();
    if (failures)
        std::fprintf(stderr, "%d sea HUD check(s) failed\n", failures);
    else
        std::printf("sea HUD checks passed\n");
    return failures ? 1 : 0;
}
