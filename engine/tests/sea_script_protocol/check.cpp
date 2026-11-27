// The sea events as each game's scripts read them (sea_script_protocol.hpp), built by the real AICannon and by the
// protocol's builders; each handler below reads in the order the game's handler calls GetEventData.
#include "core_fake.hpp"
#include "ship_fake.hpp"

#include "ai_cannon.h"
#include "sea_script_protocol.hpp"
#include "storm/render/effects.hpp"

#include <catch2/catch.hpp>

#include <functional>
#include <map>
#include <string>

namespace
{
storm::test::FakeCore fake;
storm::GameProfile profile = storm::GameProfile::Legacy;

using storm::GameProfile;
using storm::sea_ai::seaScriptProtocol;

constexpr GameProfile kProfiles[] = {GameProfile::Legacy, GameProfile::CaribbeanLegend};

// True when the next read of any type fails: the handler has read everything the event carries.
bool exhausted(MESSAGE &m)
{
    try
    {
        m.Long();
    }
    catch (const MessageFormatError &)
    {
        try
        {
            m.Float();
        }
        catch (const MessageFormatError &)
        {
            try
            {
                m.String();
            }
            catch (const MessageFormatError &)
            {
                return true;
            }
        }
    }
    return false;
}

bool fails(const std::function<void()> &read)
{
    try
    {
        read();
    }
    catch (const MessageFormatError &)
    {
        return true;
    }
    return false;
}

auto *const character = reinterpret_cast<ATTRIBUTES *>(0x1000);
auto *const enemy = reinterpret_cast<ATTRIBUTES *>(0x2000);
const glm::vec3 hit{1.0f, 2.0f, 3.0f};

void readHit(MESSAGE &m)
{
    REQUIRE(m.Float() == hit.x);
    REQUIRE(m.Float() == hit.y);
    REQUIRE(m.Float() == hit.z);
}
} // namespace

Core &core = fake;

namespace storm
{
GameProfile gameProfile()
{
    return profile;
}
namespace render
{
EffectsWorld CurrentEffectsWorld()
{
    return {}; // no flash lights: AICannon does not light its shots
}
void EffectsFlash(EffectsFlashKind, const glm::vec3 &)
{
    throw std::logic_error("EffectsFlash without flash lights");
}
} // namespace render
} // namespace storm

uint64_t get_performance_counter()
{
    return 0;
}
float AIHelper::fGravity = 9.81f;

TEST_CASE("cannon events of the real AICannon name its board in Caribbean Legend only")
{
    for (const auto game : kProfiles)
    {
        profile = game;
        const bool cl = game == GameProfile::CaribbeanLegend;

        ATTRIBUTES &chr = fake.scripts.object("character");
        AIAttributesHolder holder;
        holder.SetACharacter(&chr);
        storm::test::FakeShip ship(7, &chr, nullptr);
        fake.entityPointers[7] = &ship;

        DATA &loaded = fake.scripts.integer("loaded", 1);
        DATA &delay = fake.scripts.variable("delay", VAR_FLOAT);
        delay.Set(0.0f);

        std::vector<std::string> raised;
        fake.messageEvent = [&](std::string_view name, MESSAGE &m) -> VDATA * {
            raised.emplace_back(name);
            REQUIRE(m.AttributePointer() == &chr);
            if (name == CANNON_LOAD)
            {
                REQUIRE(exhausted(m));
                return &loaded;
            }
            if (name == CANNON_GET_RECHARGE_TIME)
            {
                if (cl)
                    REQUIRE(m.String() == "cannonl"); // bortName
                REQUIRE(exhausted(m));
                return &delay;
            }
            if (name == CANNON_GET_FIRE_TIME)
            {
                if (cl)
                {
                    REQUIRE(m.String() == "cannonl"); // sBort
                    REQUIRE(m.Long() == 1);           // isRandom: a real shot
                    REQUIRE(m.Long() == 2);           // cannonIndex
                }
                REQUIRE(exhausted(m));
                return &delay;
            }
            REQUIRE(name == CANNON_FIRE);
            for (int i = 0; i < 9;
                 i++) // fX, fY, fZ, fSpeedV0, fDirAng, fHeightAng, fCannonDirAng, fMaxFireDistance, fAngle
                m.Float();
            if (cl)
            {
                REQUIRE(m.Long() == 1);           // bMode: the engine aimed the shot
                REQUIRE(m.String() == "cannonl"); // bort
            }
            REQUIRE(exhausted(m));
            return nullptr;
        };

        // A barrel 5 m along x of the ship, pointing along its x (the locator's z axis).
        GEOS::LABEL label{};
        label.m[0][2] = -1.0f;
        label.m[1][1] = 1.0f;
        label.m[2][0] = 1.0f;
        label.m[3][0] = 5.0f;
        label.m[3][3] = 1.0f;
        AICannon cannon;
        cannon.Init(&holder, 7, label, "cannonl", 2);
        cannon.Recharge();
        cannon.Execute(0.1f); // reloaded
        REQUIRE(cannon.Fire(100.0f, glm::vec3(200.0f, 0.0f, 0.0f), true));
        cannon.Execute(0.1f); // fires and reloads
        REQUIRE(raised == std::vector<std::string>{CANNON_LOAD, CANNON_GET_RECHARGE_TIME, CANNON_GET_FIRE_TIME,
                                                   CANNON_FIRE, CANNON_LOAD, CANNON_GET_RECHARGE_TIME});
        REQUIRE(ship.shots.size() == 1);
        fake.entityPointers.erase(7);
    }
}

TEST_CASE("a reloading cannon takes the reload progress the script sets, a firing one keeps its delay")
{
    profile = GameProfile::CaribbeanLegend;
    ATTRIBUTES &chr = fake.scripts.object("character");
    AIAttributesHolder holder;
    holder.SetACharacter(&chr);
    storm::test::FakeShip ship(8, &chr, nullptr);
    fake.entityPointers[8] = &ship;
    DATA &loaded = fake.scripts.integer("loaded", 1);
    DATA &reload = fake.scripts.variable("reload", VAR_FLOAT);
    reload.Set(10.0f);
    DATA &delay = fake.scripts.variable("delay", VAR_FLOAT);
    delay.Set(5.0f);
    int shots = 0;
    fake.messageEvent = [&](std::string_view name, MESSAGE &) -> VDATA * {
        if (name == CANNON_LOAD)
            return &loaded;
        if (name == CANNON_GET_RECHARGE_TIME)
            return &reload;
        if (name == CANNON_GET_FIRE_TIME)
            return &delay;
        shots++;
        return nullptr;
    };
    GEOS::LABEL label{};
    label.m[0][0] = label.m[1][1] = label.m[2][2] = label.m[3][3] = 1.0f; // the barrel along z
    AICannon cannon;
    cannon.Init(&holder, 8, label, "cannonr", 0);
    cannon.Recharge();
    REQUIRE(cannon.GetRechargePercent() == 0.0f);
    cannon.SetRechargeProgress(0.75f);
    REQUIRE(cannon.GetRechargePercent() == Approx(0.75f));
    cannon.SetRechargeProgress(2.0f); // clamped
    REQUIRE(cannon.GetRechargePercent() == 1.0f);

    cannon.Execute(0.01f); // reloaded
    REQUIRE(cannon.Fire(100.0f, glm::vec3(0.0f, 0.0f, 200.0f), true));
    cannon.SetRechargeProgress(1.0f); // the fire delay is not a reload
    cannon.Execute(0.01f);
    REQUIRE(shots == 0);
    cannon.Execute(5.0f);
    REQUIRE(shots == 1);
    fake.entityPointers.erase(8);
}

TEST_CASE("a board's ballistics are its own, else the ship's")
{
    ATTRIBUTES &chr = fake.scripts.object("gunner");
    REQUIRE_THROWS_AS(storm::sea_ai::ReadBoardBallistics(&chr, "cannonl"), std::runtime_error);

    ATTRIBUTES *cannons = chr.CreateSubAClass(&chr, "Ship.Cannons");
    cannons->SetAttribute("SpeedV0", "100");
    cannons->SetAttribute("FireAngMin", "-0.35");
    cannons->SetAttribute("FireAngMax", "0.6");
    auto ship = storm::sea_ai::ReadBoardBallistics(&chr, "cannonl");
    REQUIRE((ship.speedV0 == 100.0f && ship.fireAngMin == -0.35f && ship.fireAngMax == 0.6f));

    ATTRIBUTES *board = cannons->CreateSubAClass(cannons, "cannonr");
    board->SetAttribute("SpeedV0", "130");
    board->SetAttribute("FireAngMin", "-0.2");
    board->SetAttribute("FireAngMax", "0.4");
    const auto right = storm::sea_ai::ReadBoardBallistics(&chr, "cannonr");
    REQUIRE((right.speedV0 == 130.0f && right.fireAngMin == -0.2f && right.fireAngMax == 0.4f));
    ship = storm::sea_ai::ReadBoardBallistics(&chr, "cannonl"); // the other board keeps the ship's
    REQUIRE(ship.speedV0 == 100.0f);
}

TEST_CASE("ball events carry the ball's cannon type in Caribbean Legend only")
{
    for (const auto game : kProfiles)
    {
        const auto &p = seaScriptProtocol(game);
        const bool cl = game == GameProfile::CaribbeanLegend;
        MESSAGE m;

        // Ball_WaterHit, Ball_IslandHit, Ball_FortHit: iCharacterIndex, x, y, z, iCannonType
        storm::sea_ai::ResetBallHit(m, p, 4, hit, 11);
        REQUIRE(m.Long() == 4);
        readHit(m);
        if (cl)
            REQUIRE(m.Long() == 11);
        REQUIRE(exhausted(m));

        // Bll_FlyNCam: x, y, z, GoodIndex
        storm::sea_ai::ResetBallNearCamera(m, p, hit, 2);
        readHit(m);
        if (cl)
            REQUIRE(m.Long() == 2);
        REQUIRE(exhausted(m));

        // Shp_HullHit: rShipObject, iBallCharacterIndex, iOurCharacterIndex, x, y, z, iFirePlaceIndex,
        // fFirePlaceDistance, iCannonType
        storm::sea_ai::ResetShipHullHit(m, p, 9, 4, 5, hit, 3, 1.5f, 11);
        REQUIRE(m.EntityID() == 9);
        REQUIRE((m.Long() == 4 && m.Long() == 5));
        readHit(m);
        REQUIRE((m.Long() == 3 && m.Float() == 1.5f));
        if (cl)
            REQUIRE(m.Long() == 11);
        REQUIRE(exhausted(m));

        // ShpSailDamage: iCharacterIndex, x, y, z, iBallCharacterIndex, iCannonType
        storm::sea_ai::ResetSailDamage(m, p, 5, hit, 4, 11);
        REQUIRE(m.Long() == 5);
        readHit(m);
        REQUIRE(m.Long() == 4);
        if (cl)
            REQUIRE(m.Long() == 11);
        REQUIRE(exhausted(m));

        // ShpMastDamage by a ball: iDamageType, iMastNum, x, y, z, fDamage, rCharacter, iBallOwnerIndex, iCannonType
        storm::sea_ai::ResetMastDamageByBall(m, p, 2, hit, 0.5f, character, 4, 11);
        REQUIRE((m.Long() == SHIP_MAST_TOUCH_BALL && m.Long() == 2));
        readHit(m);
        REQUIRE((m.Float() == 0.5f && m.AttributePointer() == character && m.Long() == 4));
        if (cl)
            REQUIRE(m.Long() == 11);
        REQUIRE(exhausted(m));

        // FortCDamage: iBallCharacterIndex, iFortCharacterIndex, aFortLabel, iNumAllCannons, iNumDamagedCannons,
        // x, y, z, fDistance, fDamage, iCannonType
        storm::sea_ai::ResetFortCannonDamage(m, p, {4, 6, enemy, 30, 2, hit, 0.8f, 0.25f}, 11);
        REQUIRE((m.Long() == 4 && m.Long() == 6 && m.AttributePointer() == enemy));
        REQUIRE((m.Long() == 30 && m.Long() == 2));
        readHit(m);
        REQUIRE((m.Float() == 0.8f && m.Float() == 0.25f));
        if (cl)
            REQUIRE(m.Long() == 11);
        REQUIRE(exhausted(m));
    }
}

TEST_CASE("cannon events built for a board, as each game's handler reads them")
{
    for (const auto game : kProfiles)
    {
        const auto &p = seaScriptProtocol(game);
        const bool cl = game == GameProfile::CaribbeanLegend;
        MESSAGE m;

        // CannGFHeight: aCharacter, aEnemy, sBort
        storm::sea_ai::ResetCannonFireHeight(m, p, character, enemy, "cannonf");
        REQUIRE((m.AttributePointer() == character && m.AttributePointer() == enemy));
        if (cl)
            REQUIRE(m.String() == "cannonf");
        REQUIRE(exhausted(m));

        // CannGFTime for the lead estimate: aCharacter, sBort, isRandom, cannonIndex
        storm::sea_ai::ResetCannonFireTime(m, p, character, "cannonf", false, -1);
        REQUIRE(m.AttributePointer() == character);
        if (cl)
            REQUIRE((m.String() == "cannonf" && m.Long() == 0 && m.Long() == -1));
        REQUIRE(exhausted(m));

        // CannDamage: aCharacter, fBallDamage, fCurDamage, fDistance, x, y, z, sBort
        storm::sea_ai::ResetCannonDamage(m, p, character, 40.0f, 0.5f, 1.25f, hit, "cannonb");
        REQUIRE(m.AttributePointer() == character);
        REQUIRE((m.Float() == 40.0f && m.Float() == 0.5f && m.Float() == 1.25f));
        readHit(m);
        if (cl)
            REQUIRE(m.String() == "cannonb");
        REQUIRE(exhausted(m));
    }
}

TEST_CASE("ship events whose layout differs between the games")
{
    for (const auto game : kProfiles)
    {
        const auto &p = seaScriptProtocol(game);
        const bool cl = game == GameProfile::CaribbeanLegend;
        MESSAGE m;

        // ShpHullDamage: iDamageType, iHullNum, x, y, z, fDamage, rCharacter, then To Each His Own: the ball owner's
        // character, HullName; Caribbean Legend: HullName, iCannonType
        storm::sea_ai::ResetHullDamage(m, p, 7, hit, 0.25f, character, enemy, "baller_1", 11);
        REQUIRE((m.Long() == SHIP_HULL_TOUCH_BALL && m.Long() == 7));
        readHit(m);
        REQUIRE((m.Float() == 0.25f && m.AttributePointer() == character));
        if (cl)
            REQUIRE((m.String() == "baller_1" && m.Long() == 11));
        else
            REQUIRE((m.AttributePointer() == enemy && m.String() == "baller_1"));
        REQUIRE(exhausted(m));

        // Ship_BortFire: the character's index, firedShip, bortName, then Caribbean Legend's sMode, then the target
        // point and the direction to it
        storm::sea_ai::ResetBortFire(m, p, 5, 9, "cannonr", hit, glm::vec3(0.0f, 0.0f, 1.0f));
        REQUIRE((m.Long() == 5 && m.EntityID() == 9 && m.String() == "cannonr"));
        if (cl)
            REQUIRE(m.String() == "auto");
        readHit(m);
        REQUIRE((m.Float() == 0.0f && m.Float() == 0.0f && m.Float() == 1.0f));
        REQUIRE(exhausted(m));

        // Ship_BortFire from the aiming camera: the character's index, firedShip, bortName, sMode
        storm::sea_ai::ResetAimedBortFire(m, 5, 9, "cannonl");
        REQUIRE((m.Long() == 5 && m.EntityID() == 9 && m.String() == "cannonl" && m.String() == "aiming"));
        REQUIRE(exhausted(m));

        // Shp_GetRunawayPnt: the character, To Each His Own also the ship's x and z
        storm::sea_ai::ResetRunawayPoint(m, p, character, 10.0f, 20.0f);
        REQUIRE(m.AttributePointer() == character);
        if (!cl)
            REQUIRE((m.Float() == 10.0f && m.Float() == 20.0f));
        REQUIRE(exhausted(m));
    }
}

TEST_CASE("a handler of one game cannot read the other game's layout")
{
    MESSAGE m;
    // Caribbean Legend's Cannon_GetRechargeTime reads the board after the character.
    storm::sea_ai::ResetCannonRechargeTime(m, seaScriptProtocol(GameProfile::Legacy), character, "cannonl");
    m.AttributePointer();
    REQUIRE(fails([&] { m.String(); }));
    // To Each His Own's Ship_HullDamage reads the ball owner's character after the hit one.
    storm::sea_ai::ResetHullDamage(m, seaScriptProtocol(GameProfile::CaribbeanLegend), 7, hit, 0.25f, character, enemy,
                                   "hull1", 11);
    for (int i = 0; i < 2; i++)
        m.Long();
    for (int i = 0; i < 4; i++)
        m.Float();
    m.AttributePointer();
    REQUIRE(fails([&] { m.AttributePointer(); }));
}

TEST_CASE("each game keeps its own board geometry")
{
    const storm::sea_ai::BoardBallistics cannons{100.0f, -0.2f, 0.4f};
    const storm::sea_ai::BoardAngles shipType{-0.35f, 0.6f};

    // To Each His Own: the ship type's zone and limits.
    const auto &legacy = seaScriptProtocol(GameProfile::Legacy);
    REQUIRE(storm::sea_ai::BoardFireZone(legacy, 2.0f) == 2.0f);
    const auto teho = storm::sea_ai::SelectBoardAngles(legacy, shipType, cannons);
    REQUIRE((teho.fireAngMin == -0.35f && teho.fireAngMax == 0.6f));

    // Caribbean Legend: three quarters of the zone, its cannons' limits.
    const auto &cl = seaScriptProtocol(GameProfile::CaribbeanLegend);
    REQUIRE(storm::sea_ai::BoardFireZone(cl, 2.0f) == 1.5f);
    const auto clAngles = storm::sea_ai::SelectBoardAngles(cl, shipType, cannons);
    REQUIRE((clAngles.fireAngMin == -0.2f && clAngles.fireAngMax == 0.4f));
}

TEST_CASE("a shot the player aimed is not an automatic one")
{
    profile = GameProfile::CaribbeanLegend;
    ATTRIBUTES &chr = fake.scripts.object("character");
    AIAttributesHolder holder;
    holder.SetACharacter(&chr);
    storm::test::FakeShip ship(9, &chr, nullptr);
    fake.entityPointers[9] = &ship;
    DATA &loaded = fake.scripts.integer("loaded", 1);
    DATA &delay = fake.scripts.variable("delay", VAR_FLOAT);
    delay.Set(0.0f);
    std::vector<int32_t> modes;
    fake.messageEvent = [&](std::string_view name, MESSAGE &m) -> VDATA * {
        if (name == CANNON_LOAD)
            return &loaded;
        if (name != CANNON_FIRE)
            return &delay;
        m.AttributePointer();
        for (int i = 0; i < 9; i++)
            m.Float();
        modes.push_back(m.Long()); // bMode
        return nullptr;
    };
    GEOS::LABEL label{};
    label.m[0][0] = label.m[1][1] = label.m[2][2] = label.m[3][3] = 1.0f;
    AICannon cannon;
    cannon.Init(&holder, 9, label, "cannonf", 0);
    for (const bool automatic : {false, true})
    {
        cannon.Recharge();
        cannon.Execute(0.01f);
        REQUIRE(cannon.Fire(100.0f, glm::vec3(0.0f, 0.0f, 200.0f), automatic));
        cannon.Execute(0.01f);
    }
    REQUIRE(modes == std::vector<int32_t>{0, 1});
    fake.entityPointers.erase(9);
}
