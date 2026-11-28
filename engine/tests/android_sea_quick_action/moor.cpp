// The sea quick action's reader (sea_quick_action_source.hpp) on the real attributes and script variables, with the
// island's reload locators as the game declares them (islands_init.c): `reload.l1` holds no value of its own, only
// children. The Moor offer names that locator object, and reading it must not touch a value it does not have.
#include "core_fake.hpp"
#include "sea_quick_action_source.hpp"
#include "ship_fake.hpp"

#include <catch2/catch.hpp>

#include <memory>

static storm::test::FakeCore fake;
Core &core = fake;
SHIP_DESCRIBE_LIST g_ShipList;

using namespace storm::android;

static void set(ATTRIBUTES &root, const char *path, const char *value)
{
    root.CreateSubAClass(&root, path)->SetValue(value);
}

TEST_CASE("Moor is offered for the island's reload locator, whose object holds no value")
{
    constexpr entid_t battleId = 5, shipId = 20, logId = 9;
    auto &scripts = fake.scripts;
    fake.event = [](std::string_view) {}; // BI_GetData unanswered: the list's defaults

    scripts.integer("bSeaActive", 1);
    scripts.text("curKeyGroupName", "Sailing1Pers");
    auto &characters = scripts.variable("Characters", VAR_OBJECT, 2);
    ATTRIBUTES &hero = *characters.GetAClass(0);
    hero.SetAttribute("index", "0");
    hero.CreateAttribute("Ship");
    set(scripts.object("BattleInterface"), "Commands.Moor.note", "Moor");

    // Islands[n].reload.l1 as islands_init.c writes it, and the script's aref to `reload` (AIShip.c).
    ATTRIBUTES &island = scripts.object("Island");
    island.SetAttribute("id", "Martinique");
    set(island, "reload.l1.label", "FortFrance Port");
    set(island, "reload.l1.name", "reload_1");
    set(island, "reload.l1.go", "FortFrance_town");
    set(island, "reload.l1.emerge", "reload1");
    set(island, "reload.l2.name", "reload_2");
    set(island, "reload.l2.go", "Shore39");
    REQUIRE_FALSE(island.FindAClass(&island, "reload.l1")->HasValue());
    scripts.variable("arIslandReload", VAR_AREFERENCE).SetAReference(island.GetAttributeClass("reload"));
    scripts.text("sIslandID", "Martinique");
    scripts.text("sIslandLocator", "reload_1");

    ATTRIBUTES shipType(scripts.codec);
    auto ship = std::make_unique<storm::test::FakeShip>(shipId, &hero, &shipType);
    fake.entityPointers[battleId] = nullptr; // the ship list's owner lives
    fake.entityPointers[shipId] = ship.get();
    fake.entityLists["ship"] = {shipId};
    g_ShipList.Bind(battleId);
    g_ShipList.Add(0, 0, &hero, &shipType, true, true, 0, 0);
    seaQuickLifecycleReady = true;

    ATTRIBUTES log(scripts.codec);
    log.SetAttribute("type", "sea");
    const auto frame = readSeaQuickAction(logId, &log, "Moor", true);
    REQUIRE(frame.complete);
    CHECK(frame.label == "Moor");
    CHECK(frame.target.find("4:name8:reload_1") != std::string::npos);
    CHECK(frame.target.find("FortFrance_town") != std::string::npos);
    CHECK(frame.target.find("Shore39") == std::string::npos); // the other locator is not the target
}
