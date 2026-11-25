// The gameplay snapshot the touch HUD is built from (readGameplaySnapshot, android_gameplay_state.hpp), read from the
// scripts' variables and the engine's entities: the mode, the hard guards that leave it unknown, the controls offered
// and who owns the input.
#include "android_gameplay_state.hpp"
#include "core_fake.hpp"
#include "ship_fake.hpp"

#include <catch2/catch.hpp>

#include <algorithm>

static storm::test::FakeCore fake;
Core &core = fake;

using namespace storm::android;

namespace
{
constexpr entid_t player = 7, location = 8;

bool offers(const GameplaySnapshot &snapshot, const char *control)
{
    return std::find(snapshot.capabilities.begin(), snapshot.capabilities.end(), control) !=
           snapshot.capabilities.end();
}

// A hero on land in a town, the PrimaryLand and FightModeControls key groups bound.
struct Game
{
    storm::test::Scripts &scripts = fake.scripts;
    DATA &characters = scripts.variable("Characters", VAR_OBJECT, 2);
    ATTRIBUTES &hero = *characters.GetAClass(0);
    ATTRIBUTES &other = *characters.GetAClass(1);
    ATTRIBUTES &interface = scripts.object("InterfaceStates");
    ATTRIBUTES &controls = scripts.object("objControlsState");
    ATTRIBUTES &land = scripts.object("objLandInterface");
    ATTRIBUTES &battle = scripts.object("BattleInterface");
    ATTRIBUTES &groups = controls.CreateAttribute("keygroups");
    bool fighting = false;

    Game()
    {
        fake.entities = {{"Player", player}, {"location", location}};
        fake.entityAttributes = {{player, &hero}};
        fake.message = [this](entid_t to, const std::vector<std::string> &arguments) -> uint64_t {
            REQUIRE(to == fake.entities.at("Player"));
            REQUIRE(arguments.at(1) == "IsFightMode");
            return fighting;
        };
        scripts.integer("nMainCharacterIndex", 0);
        scripts.text("curKeyGroupName", "PrimaryLand");
        scripts.integer("bLandInterfaceStart", 1);
        scripts.integer("dialogRun", 0);
        hero.SetAttribute("location", "city");
        hero.CreateSubAClass(&hero, "chr_ai.type")->SetValue("player");
        for (const char *name : {"PrimaryLand", "FightModeControls"})
        {
            auto &group = groups.CreateAttribute(name);
            for (const char *control : {"ChrForward", "ChrBackward", "ChrStrafeLeft", "ChrStrafeRight", "ChrFightMode"})
                group.CreateAttribute(control);
            group.CreateAttribute(std::string(name) == "PrimaryLand" ? "ChrAction" : "ChrAttackBase");
        }
    }
    ~Game()
    {
        for (const char *name :
             {"Characters", "InterfaceStates", "objControlsState", "objLandInterface", "BattleInterface",
              "nMainCharacterIndex", "curKeyGroupName", "bLandInterfaceStart", "dialogRun", "bSeaActive"})
            scripts.remove(name);
        fake.entityLists.clear();
        fake.entityPointers.clear();
    }
    void group(const char *name)
    {
        scripts.text("curKeyGroupName", name);
    }
    static GameplaySnapshot read()
    {
        return readGameplaySnapshot(true);
    }
    static void unknown()
    {
        const auto snapshot = read();
        CHECK(snapshot.mode == "unknown");
        CHECK(snapshot.inputOwner.empty());
        CHECK(snapshot.capabilities.empty());
        CHECK_FALSE(snapshot.blocked.empty());
    }
};
} // namespace

TEST_CASE("on land the hero's own stance decides, whatever key group the scripts left")
{
    Game game;
    const auto start = Game::read();
    REQUIRE(start.mode == "explore");
    REQUIRE_FALSE(start.inputOwner.empty());
    for (const char *group : {"PrimaryLand", "FightModeControls"})
        for (bool fight : {false, true})
        {
            CAPTURE(group, fight);
            game.group(group);
            game.fighting = fight;
            const auto next = Game::read();
            CHECK(next.mode == (fight ? "melee" : "explore"));
            CHECK(next.inputOwner == start.inputOwner);
            for (const char *movement : {"ChrForward", "ChrBackward", "ChrStrafeLeft", "ChrStrafeRight"})
                CHECK(offers(next, movement));
            if (std::string(group) == "PrimaryLand") // a stale group does not invent the fight group's bindings
                CHECK_FALSE(offers(next, "ChrAttackBase"));
        }
}

TEST_CASE("hard guards leave the mode unknown")
{
    Game game;
    SECTION("a dialogue")
    {
        game.scripts.integer("dialogRun", 1);
    }
    SECTION("an interface")
    {
        game.interface.SetAttributeUseDword("Launched", 1);
    }
    SECTION("a cutscene actor")
    {
        game.hero.FindAClass(&game.hero, "chr_ai.type")->SetValue("actor");
    }
    SECTION("the land command menu")
    {
        game.land.SetAttributeUseDword("ComState", 1);
    }
    SECTION("the battle command menu")
    {
        game.battle.SetAttributeUseDword("ComState", 1);
    }
    SECTION("a video")
    {
        fake.entities["CAviPlayer"] = 10;
    }
    SECTION("a world map video")
    {
        fake.entities["WMVideoPlay"] = 10;
    }
    SECTION("a scene change under way")
    {
        fake.entities["fader"] = 10;
    }
    SECTION("the battle interface's key group")
    {
        game.group("BattleInterfaceControls");
    }
    SECTION("no land interface")
    {
        game.scripts.integer("bLandInterfaceStart", 0);
    }
    SECTION("no player entity")
    {
        fake.entities.erase("Player");
    }
    SECTION("a player entity of another character")
    {
        fake.entityAttributes[player] = &game.other;
    }
    Game::unknown();
}

TEST_CASE("not ready: starting")
{
    Game game;
    const auto snapshot = readGameplaySnapshot(false);
    CHECK(snapshot.mode == "unknown");
    CHECK(snapshot.blocked == "starting");
    CHECK(snapshot.inputOwner.empty());
}

TEST_CASE("the world map: its own controls and time steps, never stale sea flags")
{
    Game game;
    auto &map = game.groups.CreateAttribute("WorldMapControls");
    for (const char *control : {"WMapShipSailUp", "WMapShipSailDown", "WMapShipTurnLeft", "WMapShipTurnRight",
                                "WMapCancel", "WMapCameraSwitch"})
        map.CreateAttribute(control);
    fake.entities["WorldMap"] = 21;
    game.group("WorldMapControls");
    game.scripts.integer("bSeaActive", 1);
    const auto snapshot = Game::read();
    CHECK(snapshot.mode == "worldmap");
    CHECK(snapshot.inputOwner.empty());
    CHECK(snapshot.capabilities.size() == 6);
    CHECK_FALSE(offers(snapshot, "ChrAction"));
    auto &created = game.controls.CreateAttribute("map").CreateAttribute("controls");
    created.CreateAttribute("TimeScaleFaster");
    created.CreateAttribute("TimeScaleSlower");
    const auto timed = Game::read();
    for (const char *step : {"TimeScaleX1", "TimeScaleX2", "TimeScaleX4", "TimeScaleX8"})
        CHECK(offers(timed, step));
    fake.entities["WorldMap"] = 22;
    CHECK(Game::read().scene != snapshot.scene);
    fake.entities.erase("WorldMap");
    Game::unknown(); // the group alone does not give map input
}

TEST_CASE("at sea the spyglass is offered only with a spyglass equipped")
{
    Game game;
    storm::test::Codec codec;
    ATTRIBUTES shipType(codec);
    storm::test::FakeShip ship(20, &game.hero, &shipType);
    fake.entityLists["ship"] = {20};
    fake.entityPointers[20] = &ship;
    game.scripts.integer("bSeaActive", 1);
    auto &deck = game.groups.CreateAttribute("Sailing1Pers");
    for (const char *control : {"Ship_TurnLeft", "TelescopeIn", "TelescopeOut"})
        deck.CreateAttribute(control);
    game.group("Sailing1Pers");
    auto sailing = Game::read();
    CHECK(sailing.mode == "sailing");
    CHECK(offers(sailing, "Ship_TurnLeft"));
    CHECK_FALSE(offers(sailing, "TelescopeIn"));
    CHECK_FALSE(offers(sailing, "TelescopeOut"));
    auto &equip = game.hero.CreateAttribute("equip");
    equip.SetAttribute("spyglass", "");
    CHECK_FALSE(offers(Game::read(), "TelescopeIn")); // an emptied slot is no spyglass
    equip.SetAttribute("spyglass", "spyglass1");
    sailing = Game::read();
    CHECK(offers(sailing, "TelescopeIn"));
    CHECK(offers(sailing, "TelescopeOut"));
}

TEST_CASE("the input belongs to the hero in this place")
{
    Game game;
    const auto start = Game::read();
    fake.entities["Player"] = 9;
    fake.entityAttributes[9] = &game.hero;
    CHECK(Game::read().inputOwner != start.inputOwner);
    fake.entities["Player"] = player;
    fake.entities["location"] = 10;
    CHECK(Game::read().inputOwner != start.inputOwner);
    fake.entities["location"] = location;
    game.hero.SetAttribute("location", "other-city");
    CHECK(Game::read().inputOwner != start.inputOwner);
    game.hero.SetAttribute("location", "city");
    CHECK(Game::read().inputOwner == start.inputOwner);
}

TEST_CASE("a control the scripts unbound is not offered")
{
    Game game;
    auto *primary = game.groups.GetAttributeClass("PrimaryLand");
    primary->DeleteAttributeClassX(primary->GetAttributeClass("ChrForward"));
    CHECK_FALSE(offers(Game::read(), "ChrForward"));
}
