// The world map HUD's mailbox and state (android_worldmap.hpp) and the command adapters of the stock map interface
// (android_map_commands.hpp) on a stand-in command list and map interface.
#include "android_map_commands.hpp"

#include <spdlog/sinks/ringbuffer_sink.h>

#include <cassert>
#include <iostream>
using namespace storm::android;
// The stock command list: its rows and its selection.
struct WMShipCommandList
{
    struct Item
    {
        std::string sCommandName, sNote;
    };
    std::vector<Item> m_aUsedCommand;
    std::string selected;
    bool SelectAndroidMapCommand(const std::string &id)
    {
        const auto row = mapCommandRow(m_aUsedCommand, id);
        if (row)
            selected = "i" + std::to_string(*row);
        return row.has_value();
    }
};
// The stock map interface: what activateMapCommand uses of it.
struct WM_INTERFACE
{
    bool m_bVisible = true, active = false, stopOnPurpose = false;
    int m_nCommandMode = 0, executed = 0;
    void MarkStopOnPurpose()
    {
        assert(!executed); // before the command leaves the map
        stopOnPurpose = true;
    }
    WMShipCommandList *m_pCommandList = nullptr;
    std::vector<WMShipCommandList::Item> fresh;
    bool IsCommandMenuActive()
    {
        return active;
    }
    void UpdateCommandList()
    {
        m_pCommandList->m_aUsedCommand = fresh;
    }
    void ExecuteCommand(int command)
    {
        executed = command;
    }
};
WorldMapFrame complete()
{
    WorldMapFrame f;
    f.available = true;
    f.owner = 19;
    f.gameplayRevision = 4;
    f.identity = "profile:captain:19";
    f.context = "island:Martinique";
    f.instrumentsVisible = f.shipsVisible = f.commandsReady = true;
    auto &i = f.instruments;
    i.owner = 19;
    i.complete = true;
    i.visible = true;
    i.date = "18 October 1664";
    i.hour = 12.25;
    i.windForce = .5;
    i.windAngle = 1;
    i.heading = 2;
    i.morale = 50;
    i.foodDays = 17;
    i.rumDays = 20;
    i.flag = 2;
    i.coordinates = "15 N";
    f.ships.push_back({"0:captain:22", "Adelina", 100, 98, 40, 6});
    f.commands.push_back({"EnterToIsland", "Approach island"});
    return f;
}

int main()
{
    assert(storm::android::mapStormState("0", "0") == 0);
    assert(storm::android::mapStormState("1", "0") == 1);
    assert(storm::android::mapStormState("1", "1") == 2);
    assert(!storm::android::mapStormState(nullptr, "0"));
    assert(!storm::android::mapStormState("1", nullptr));
    assert(!storm::android::mapStormState("bad", "0"));
    assert(!storm::android::mapStormState("0", "1"));

    // The moon of the game's sky (WhrSun.c: Moon00…Moon07 by day / 4).
    assert(storm::android::mapMoonPhase(1) == 0 && storm::android::mapMoonPhase(3) == 0 &&
           storm::android::mapMoonPhase(4) == 1);
    assert(storm::android::mapMoonPhase(16) == 4 && storm::android::mapMoonPhase(19) == 4 &&
           storm::android::mapMoonPhase(31) == 7);
    assert(!storm::android::mapMoonPhase(0) && !storm::android::mapMoonPhase(32));
    WorldMapHud weatherHud;
    auto weatherFrame = complete();
    weatherHud.update(weatherFrame);
    const auto weatherRevision = weatherHud.revision();
    assert(weatherHud.hud().storm == -1);
    for (int storm = 0; storm <= 2; ++storm)
    {
        weatherFrame.instruments.stormState = storm;
        weatherHud.update(weatherFrame);
        assert(weatherHud.revision() == weatherRevision); // Weather alone must not revoke held input/commands.
        assert(weatherHud.hud().storm == storm);
    }
    assert(weatherHud.hud().moon_phase == -1);
    weatherFrame.instruments.moonPhase = 6;
    weatherHud.update(weatherFrame);
    assert(weatherHud.revision() == weatherRevision);
    assert(weatherHud.hud().moon_phase == 6);
    WorldMapHud betweenFrames;
    betweenFrames.update(complete());
    betweenFrames.resetActivity();
    // The drawn UI compass's full box is accepted only for this owner/activity.
    using Area = std::array<uint32_t, 6>;
    const auto packet = [](WorldMapHud &h, std::optional<Area> area) {
        return storm::ui::hud::MapLabelArea{h.revision(), area};
    };
    const auto labelArea = packet(betweenFrames, Area{960, 540, 702, 15, 240, 219});
    assert(betweenFrames.offer(labelArea));
    assert(betweenFrames.labelCompass() && (*betweenFrames.labelCompass())[4] == 240);
    assert(!betweenFrames.offer(packet(betweenFrames, Area{960, 540, 900, 15, 240, 219})));
    assert(!betweenFrames.offer(packet(betweenFrames, Area{960, 0, 702, 15, 240, 219})));
    assert(betweenFrames.labelCompass()); // malformed geometry cannot overwrite the last drawn box
    assert(betweenFrames.offer(packet(betweenFrames, std::nullopt)));
    assert(!betweenFrames.labelCompass());
    assert(betweenFrames.offer(labelArea));
    betweenFrames.resetActivity();
    assert(!betweenFrames.labelCompass() && !betweenFrames.offer(labelArea));
    const auto freshArea = packet(betweenFrames, Area{960, 540, 702, 15, 240, 219});
    assert(betweenFrames.offer(freshArea) && betweenFrames.labelCompass());
    auto changedFrame = complete();
    changedFrame.owner = 20;
    betweenFrames.update(changedFrame);
    assert(!betweenFrames.labelCompass() && !betweenFrames.offer(freshArea));
    // A command the HUD has no place for is reported in the log.
    const auto log = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(8);
    spdlog::default_logger()->sinks().push_back(log);
    WMShipCommandList commands;
    commands.m_aUsedCommand = {{"Cancel", "Cancel"}, {"EnterToIsland", "Approach island"}};
    assert(mapCommands(commands.m_aUsedCommand).size() == 1 &&
           mapCommands(commands.m_aUsedCommand)[0].id == "EnterToIsland");
    commands.m_aUsedCommand[0].sCommandName = "cancel";
    assert(mapCommands(commands.m_aUsedCommand).size() == 1);
    commands.m_aUsedCommand[1].sNote = "";
    assert(mapCommands(commands.m_aUsedCommand).empty());
    commands.m_aUsedCommand[1] = {"EnterToMoon", "Fly to the moon"};
    assert(mapCommands(commands.m_aUsedCommand).empty() &&
           log->last_formatted().size() == 1); // no button for it: said, none drawn
    commands.m_aUsedCommand[1] = {"EnterToIsland", "Approach island"};
    WM_INTERFACE interface;
    interface.m_pCommandList = &commands;
    interface.fresh = {{"Cancel", "Cancel"}, {"EnterToIsland", "Approach island"}};
    assert(!activateMapCommand(interface, "EnterToSea") && !interface.executed);
    assert(activateMapCommand(interface, "EnterToIsland") && interface.executed == BI_MSG_COMMAND_ACTIVATE &&
           commands.selected == "i1");
    // Leaving for the open sea is a stop on purpose (WMapCancel's script half, first): an
    // encounter met there can be sailed past. A failed selection marks nothing.
    assert(interface.stopOnPurpose);
    {
        WM_INTERFACE engage;
        engage.m_pCommandList = &commands;
        engage.fresh = {{"EnterToAttack", "Attack"}, {"EnterToSea", "To sea"}};
        assert(activateMapCommand(engage, "EnterToAttack") && engage.executed && !engage.stopOnPurpose);
        WM_INTERFACE missing;
        missing.m_pCommandList = &commands;
        missing.fresh = {{"EnterToAttack", "Attack"}};
        assert(!activateMapCommand(missing, "EnterToSea") && !missing.stopOnPurpose);
        WM_INTERFACE sea;
        sea.m_pCommandList = &commands;
        sea.fresh = {{"EnterToSea", "To sea"}};
        assert(activateMapCommand(sea, "EnterToSea") && sea.stopOnPurpose && sea.executed);
        assert(leavesForOpenSea("EnterToSea") && leavesForOpenSea("EnterToIsland") &&
               !leavesForOpenSea("EnterToShip") && !leavesForOpenSea("EnterToStorm") &&
               !leavesForOpenSea("EnterToEnemy"));
    }
    interface.active = true;
    assert(!activateMapCommand(interface, "EnterToIsland"));
    interface.active = false;
    interface.m_bVisible = false;
    assert(!activateMapCommand(interface, "EnterToIsland"));
    WorldMapHud h;
    auto f = complete();
    h.update(f);
    h.resetActivity();
    auto request = [&](std::string id) { return storm::ui::hud::MapCommand{h.revision(), id}; };
    assert(f.availableMask() == 7);
    const auto old = request("EnterToIsland");
    assert(h.offer(old));
    f.context = "island:Guadeloupe";
    h.update(f);
    assert(!h.take());
    assert(!h.offer(old));
    assert(!h.offer(request("EnterToEnemy")));
    assert(h.offer(request("EnterToIsland")));
    assert(!h.offer(request("EnterToIsland")));
    assert(h.take() == "EnterToIsland");
    assert(!h.take());
    auto rev = h.revision();
    f.instruments.hour = 14;
    h.update(f);
    assert(h.revision() == rev); // Numeric telemetry keeps held controls stable.
    f.instruments.foodDays.reset();
    h.update(f);
    assert(h.frame().availableMask() == 6);
    f.commandsReady = false;
    h.update(f);
    assert(!h.offer(request("EnterToIsland")));
    f = complete();
    h.update(f);
    assert(h.offer(request("EnterToIsland")));
    assert(h.take() == "EnterToIsland");
    assert(h.offer(request("EnterToIsland")));
    const auto staleActivity = request("EnterToIsland");
    h.resetActivity();
    assert(!h.offer(staleActivity));
    h.update({});
    assert(!h.offer(staleActivity));
    assert(!h.take());
    h.update(complete());
    assert(h.offer(request("EnterToIsland")) && h.take() == "EnterToIsland");
    auto before = h.revision();
    auto changed = complete();
    changed.instruments.flag.reset();
    h.update(changed);
    assert(h.revision() != before);
    changed.instruments.rumDays.reset();
    changed.instruments.coordinates.clear();
    h.update(changed);
    assert(h.frame().availableMask() == 7);
    changed.shipsComplete = false;
    h.update(changed);
    assert(h.frame().availableMask() == 5);
    h.update(complete());
    const auto shown = h.hud();
    assert(shown.food_days == 17 && !shown.fleet.empty() && shown.fleet[0].name == "Adelina");
    { // the sign's ship picture and the crew capacity go with the ship
        auto pictured = complete();
        pictured.ships[0].crewMax = 80;
        pictured.ships[0].picture =
            shipPicture("battle_interface\\ship_icons2.tga.tx", {0.25f, 0.0625f, 0.3125f, 0.125f});
        WorldMapHud p;
        p.update(pictured);
        const auto out = p.hud();
        assert(out.fleet[0].crew == 0.5); // 40 of 80
        assert(out.fleet[0].picture.texture == "battle_interface/ship_icons2" &&
               (out.fleet[0].picture.uv == std::array<float, 4>{0.25f, 0.0625f, 0.3125f, 0.125f}));
        assert(shown.fleet[0].picture.texture.empty() && shown.fleet[0].crew == -1.0); // without a capacity: unknown
    }
    {
        // What the HUD moves (the sail lever, the compass, the wind, the clock and the sun or moon it
        // places) goes out in the frame its drawn value changes; the rest waits for the 200 ms beat.
        WorldMapHud p;
        auto live = complete();
        live.instruments.speed = 0.2;
        p.update(live);
        assert(p.due(1000));
        p.published(1000);
        assert(!p.due(1016));
        live.instruments.morale = 49;
        p.update(live);
        assert(!p.due(1016) && p.due(1200)); // not drawn every frame
        live.instruments.hour = 12.255;
        p.update(live);
        assert(!p.due(1016)); // the same minute (12:15)
        live.instruments.hour = 12.27;
        p.update(live);
        assert(p.due(1016));
        p.published(1016); // 12:16
        live.instruments.windAngle = 1.003;
        p.update(live);
        assert(!p.due(1025)); // the same whole degree
        live.instruments.windAngle = 1.01;
        p.update(live);
        assert(p.due(1025));
        p.published(1025);
        live.instruments.windForce = .504;
        p.update(live);
        assert(!p.due(1029)); // the same hundredth
        live.instruments.windForce = .51;
        p.update(live);
        assert(p.due(1029));
        p.published(1029);
        live.instruments.speed = 0.21;
        p.update(live);
        assert(p.due(1033));
        p.published(1033);
        live.instruments.heading = 2.001;
        p.update(live);
        assert(!p.due(1050)); // the same whole degree
        live.instruments.heading = 2.02;
        p.update(live);
        assert(p.due(1050));
        p.published(1050);
        p.resetActivity();
        assert(p.due(1066));
        p.published(1066);
        live.context = "island:Guadeloupe";
        p.update(live);
        assert(p.due(1083)); // a new revision
        p.published(UINT32_MAX - 5);
        assert(!p.due(10) && p.due(200)); // tick rollover
    }
    std::cout << "PASS map command identity, one-shot dispatch, quest lock, release, "
                 "activity and tick rollover\n";
}
