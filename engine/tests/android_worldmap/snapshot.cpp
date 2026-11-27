// The world map HUD's frame read from the scripts (readWorldMapFrame, android_worldmap_state.hpp) and the ship entry
// of the sea HUD (shipEntry, android_ship_state.hpp), built as for Android against the fake Core.
#include "android_worldmap_state.hpp"
#include "core_fake.hpp"
#include "ship_fake.hpp"

#include <cassert>
#include <iostream>

static storm::test::FakeCore fake;
Core &core = fake;

namespace storm::android
{
// What other modules answer: the world map's encounter context (worldmap), its command list (battle_interface) and
// the sea HUD's ship sign (battle_interface, absent on the map).
std::string worldMapContext(uint64_t)
{
    return "Martinique";
}
void readWorldMapCommands(WorldMapFrame &f)
{
    f.shipsVisible = true;
    f.commandsReady = true;
    f.commands = {{"EnterToIsland", "Approach island"}};
}
std::optional<ShipPicture> seaShipPicture(int)
{
    return std::nullopt;
}
} // namespace storm::android

int main()
{
    using namespace storm::android;
    auto &scripts = fake.scripts;
    auto &characters = scripts.variable("Characters", VAR_OBJECT, 2);
    ATTRIBUTES &captain = *characters.GetAClass(0), &companion = *characters.GetAClass(1);
    ATTRIBUTES &profile = scripts.object("PlayerProfile");
    ATTRIBUTES &shipType = *scripts.variable("RealShips", VAR_OBJECT, 2).GetAClass(0);
    storm::test::Codec codec;
    ATTRIBUTES wmInterface(codec);
    fake.entities = {{"WorldMap", 19}, {"WM_INTERFACE", 20}};
    fake.entityAttributes = {{20, &wmInterface}};
    ATTRIBUTES &badges = wmInterface.CreateAttribute("wm_sign");
    captain.SetAttribute("index", "0");
    captain.SetAttribute("id", "hero");
    auto put = [](ATTRIBUTES &root, const char *path, const char *value) {
        root.CreateSubAClass(&root, path)->SetValue(value);
    };
    put(captain, "Ship.Name", "Adelina");
    put(captain, "Ship.Type", "0");
    put(captain, "Fellows.Companions.id1", "1");
    companion.SetAttribute("index", "1");
    companion.SetAttribute("id", "friend");
    put(companion, "Ship.Name", "Second ship");
    put(companion, "Ship.Type", "0");
    profile.SetAttribute("name", "QA_map");
    shipType.SetAttribute("Class", "6");
    shipType.SetAttribute("Name", "Lugger");
    for (const char *sign : {"sign1", "sign2"})
    {
        auto *a = badges.CreateAttribute(sign, nullptr);
        a->SetAttribute("leftprogress", "0.8");
        a->SetAttribute("rightprogress", "0.9");
        a->SetAttribute("text", "40");
    }
    badges.SetAttribute("shiptexturename", "battle_interface\\ship_icons2.tga.tx");
    shipType.SetAttribute("MaxCrew", "80");
    badges.GetAttributeClass("sign1")->SetAttribute("faceuv", "0.25,0.0625,0.3125,0.125");
    badges.GetAttributeClass("sign2")->SetAttribute("faceuv", "1,0,0.5,1");
    scripts.integer("nMainCharacterIndex", 0);
    GameplaySnapshot live;
    live.mode = "worldmap";
    live.scene = "map19";
    GameplayState game;
    game.update(live);
    gameplayReadEpoch = storm::ScriptStateEpoch(); // the snapshot is what the scripts hold now
    worldMapHud.contextActionOwner = 19;
    worldMapHud.contextAction = "EnterToSea";
    auto frame = readWorldMapFrame(game, true);
    assert(frame.available && frame.commandsReady && frame.ships.size() == 2);
    assert(frame.ships[0].name == "Adelina" && frame.ships[1].name == "Second ship");
    assert(frame.ships[0].hull == 80 && frame.ships[0].crew == 40 && frame.ships[0].shipClass == 6);
    // The sign's ship picture (faceuv in the sign texture) and the crew capacity of the ship type; a
    // malformed rectangle gives no picture rather than a wrong one.
    assert(frame.ships[0].crewMax == 80 && frame.ships[0].picture &&
           frame.ships[0].picture->texture == "battle_interface/ship_icons2" &&
           frame.ships[0].picture->uv == (std::array<float, 4>{0.25f, 0.0625f, 0.3125f, 0.125f}) &&
           !frame.ships[1].picture);
    assert(frame.ships[0].type == "Lugger" && frame.ships[1].type == "Lugger");
    WorldMapHud typedHud;
    typedHud.update(frame);
    assert(typedHud.hud().fleet[0].type == "Lugger");
    storm::test::FakeShip seaShip(31, &captain, &shipType);
    auto sea = shipEntry(&seaShip);
    assert(sea.type == "Lugger" && sea.shipClass == 6 && sea.name == "Adelina");
    // Actual stock generation keeps BaseName and appends "1" to the rendered model.
    shipType.SetAttribute("BaseName", "Lugger");
    shipType.SetAttribute("Name", "Lugger1");
    assert(shipEntry(&seaShip).type == "Lugger");
    assert(readWorldMapFrame(game, true).ships[0].type == "Lugger");
    shipType.SetAttribute("BaseName", "");
    shipType.SetAttribute("Name", "Lugger");
    for (const char *invalid : {"-1", "2.5", "8", "unknown"})
    {
        shipType.SetAttribute("Class", invalid);
        assert(!shipEntry(&seaShip).shipClass);
    }
    shipType.SetAttribute("Class", "6");
    // The optional model is a live RealShips field, not the vessel's custom name.
    put(captain, "Ship.Name", "Frigate");
    assert(shipEntry(&seaShip).type == "Lugger");
    shipType.SetAttribute("Name", "");
    assert(shipEntry(&seaShip).type.empty());
    auto unknownType = readWorldMapFrame(game, true);
    assert(unknownType.ships[0].type.empty() && unknownType.availableMask() == frame.availableMask());
    typedHud.update(unknownType);
    assert(typedHud.hud().fleet[0].type.empty());
    shipType.SetAttribute("Name", "Lugger");
    put(captain, "Ship.Name", "Adelina");
    assert(!frame.instrumentsVisible &&
           frame.availableMask() == 6); // No fake instruments before renderer supplies them.
    { // Day and night from the scripts' hourly weathers, as FindWeatherByHour takes each hour's record.
        auto &weathers = scripts.variable("Weathers", VAR_OBJECT, 26);
        for (uint32_t n = 0; n < 24; ++n)
        {
            ATTRIBUTES &record = *weathers.GetAClass(n);
            put(record, "Hour.Min", std::to_string(n).c_str());
            put(record, "Night", n < 6 || n >= 21 ? "1" : "0");
        }
        ATTRIBUTES &storm = *weathers.GetAClass(24); // a storm at noon is not noon's record
        put(storm, "Hour.Min", "12");
        put(storm, "Storm", "1");
        put(storm, "Night", "1");
        worldMapHud.source.owner = 19;
        assert((readWorldMapFrame(game, true).instruments.dayHours == std::pair{6, 21}));
        put(*weathers.GetAClass(12), "skip", "1"); // noon skipped: the storm is still no record for it
        assert(!readWorldMapFrame(game, true).instruments.dayHours);
        scripts.remove("Weathers");
        worldMapHud.source = {};
    }
    // WM_SetShipData omits land-only/stale companions, zero sentinel and slots
    // beyond COMPANION_MAX. They must not consume badge positions or poison all fleet coverage.
    put(captain, "Fellows.Companions.id1", "0");
    put(captain, "Fellows.Companions.id2", "1");
    put(companion, "Ship.Type", "-1");
    put(captain, "Fellows.Companions.id5", "1");
    badges.SetName("wm_sign_saved"); // no badges: the strict missing-badges fallback
    assert(!(readWorldMapFrame(game, true).availableMask() & MapShips));
    auto *only = wmInterface.CreateAttribute("wm_sign", nullptr)->CreateAttribute("sign1", nullptr);
    only->SetAttribute("leftprogress", "0.8");
    only->SetAttribute("rightprogress", "0.9");
    only->SetAttribute("text", "40");
    auto single = readWorldMapFrame(game, true);
    assert(single.ships.size() == 1 && single.shipsComplete && (single.availableMask() & MapShips));
    assert(single.ships.front().name == "Adelina" && single.ships.front().crew == 40);
    // A sign for a ship the frame does not have: the fleet is not complete.
    auto *extra = wmInterface.GetAttributeClass("wm_sign")->CreateAttribute("sign2", nullptr);
    assert(!readWorldMapFrame(game, true).shipsComplete);
    wmInterface.GetAttributeClass("wm_sign")->DeleteAttributeClassX(extra);
    // A valid later slot still consumes the next native sign after skipped slots.
    put(captain, "Fellows.Companions.id2", "-1");
    put(companion, "Ship.Type", "0");
    put(captain, "Fellows.Companions.id4", "1");
    wmInterface.DeleteAttributeClassX(wmInterface.GetAttributeClass("wm_sign"));
    badges.SetName("wm_sign");
    auto later = readWorldMapFrame(game, true);
    assert(later.ships.size() == 2 && later.shipsComplete && (later.availableMask() & MapShips));
    assert(later.ships[1].name == "Second ship" && later.ships[1].crew == 40);
    worldMapHud.contextAction = "Closed";
    assert(!readWorldMapFrame(game, true).commandsReady);
    worldMapHud.contextAction = "EnterToSea";
    const auto identity = frame.identity;
    profile.SetAttribute("name", " QA_map");
    assert(readWorldMapFrame(game, true).identity != identity);
    profile.SetAttribute("name", "other");
    assert(readWorldMapFrame(game, true).identity != identity);
    scripts.integer("questwdmLockReload", 1);
    assert(readWorldMapFrame(game, true).commandsReady); // a locked map refuses in the scripts' own handler
    scripts.remove("questwdmLockReload");
    live.mode = "sea";
    game.update(live);
    assert(!readWorldMapFrame(game, true).available);
    live.mode = "worldmap";
    game.update(live);
    assert(!readWorldMapFrame(game, false).available);
    scripts.remove("PlayerProfile");
    assert(!readWorldMapFrame(game, true).available);
    std::cout << "PASS actual map snapshot: own fleet badges, canonical companion filters/one ship/late slots, "
                 "identity/profile, missing data, quest guards, gameplay transition\n";
}
