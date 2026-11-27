#pragma once

// The sea events whose arguments, or whose being raised at all, differ between the games: one row per game profile
// (game_profile.hpp), and the builders that lay each event out as that game's scripts read it with GetEventData.
// Call sites build through these and never look at the profile.

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "game_profile.hpp"
#include "message.h"
#include "shared/sea_ai/script_defines.h"
#include "storm_glm.h"

namespace storm::sea_ai
{
// The hit character and what follows it in ShpHullDamage.
enum class HullDamageLayout : uint8_t
{
    OwnerHullName,      // "llffffaas": the ball owner's character, the hull node name
    HullNameCannonType, // "llffffasl": the hull node name, the ball's cannon type
};

struct SeaScriptProtocol
{
    // CannGRTime, CannGFTime, CannGFHeight, CannDamage and CannFire name the board (ship) or the cannon type key
    // (fort) of the cannon; CannGFTime adds whether the delay is drawn at random and the cannon's index, CannFire
    // whether the shot is an automatic one.
    bool board_in_cannon_events;
    // Ball_WaterHit, Ball_IslandHit, Ball_FortHit, Shp_HullHit, ShpSailDamage, FortCDamage and ShpMastDamage by a
    // ball end with the ball's cannon type; Bll_FlyNCam with the ball's goods index.
    bool cannon_type_in_ball_events;
    HullDamageLayout hull_damage;
    // Ship_Bort_Fire names how the board fired ("auto") before the target point.
    bool bort_fire_mode;
    // Shp_GetRunawayPnt passes the ship's x and z after the character.
    bool runaway_point_position;
    // A board's elevation limits come from the character's Ship.Cannons (per board, see ReadBoardBallistics) instead
    // of the ship type's Cannons.Borts.
    bool board_angles_from_character;
    // The ship type's Cannons.Borts.<board>.FireZone is narrowed by this factor.
    float fire_zone_scale;
    // A ball hitting a fort raises Fort_Damage, whose answer is the number of cannons it destroys (Fort_CannonDestroy).
    bool fort_damage_event;
    // A sinking ship raises Event_DeleteShipSailors and Event_DeleteShipFoam with its character index.
    bool ship_sink_events;
    // The tornado raises TornadoDamage and TornadoDelete.
    bool tornado_events;
};

inline constexpr std::array<SeaScriptProtocol, kGameProfiles.size()> kSeaScriptProtocols = {{
    {.board_in_cannon_events = false,
     .cannon_type_in_ball_events = false,
     .hull_damage = HullDamageLayout::OwnerHullName,
     .bort_fire_mode = false,
     .runaway_point_position = true,
     .board_angles_from_character = false,
     .fire_zone_scale = 1.0f,
     .fort_damage_event = false,
     .ship_sink_events = false,
     .tornado_events = true},
    {.board_in_cannon_events = true,
     .cannon_type_in_ball_events = true,
     .hull_damage = HullDamageLayout::HullNameCannonType,
     .bort_fire_mode = true,
     .runaway_point_position = false,
     .board_angles_from_character = true,
     .fire_zone_scale = 0.75f,
     .fort_damage_event = true,
     .ship_sink_events = true,
     .tornado_events = false},
}};

constexpr const SeaScriptProtocol &seaScriptProtocol(GameProfile profile)
{
    return kSeaScriptProtocols[static_cast<size_t>(profile)];
}

inline const SeaScriptProtocol &seaScriptProtocol()
{
    return seaScriptProtocol(gameProfile());
}

struct BoardBallistics
{
    float speedV0;
    float fireAngMin, fireAngMax; // elevation limits, radians
};

// The cannons of one board (a ship's board, or a fort's cannon type key): Ship.Cannons.<board>.{SpeedV0, FireAngMin,
// FireAngMax}, or the same names directly under Ship.Cannons where the game writes one set for the whole ship.
inline BoardBallistics ReadBoardBallistics(ATTRIBUTES *character, const std::string &board)
{
    ATTRIBUTES *cannons = character->FindAClass(character, "Ship.Cannons");
    if (!cannons)
        throw std::runtime_error(std::string("sea AI: character ") + to_string(character->GetAttribute("id")) +
                                 " has no Ship.Cannons");
    if (ATTRIBUTES *own = cannons->GetAttributeClass(board))
        cannons = own;
    return {cannons->GetAttributeAsFloat("SpeedV0"), cannons->GetAttributeAsFloat("FireAngMin"),
            cannons->GetAttributeAsFloat("FireAngMax")};
}

// CannGRTime
inline void ResetCannonRechargeTime(MESSAGE &m, const SeaScriptProtocol &p, ATTRIBUTES *character,
                                    const std::string &board)
{
    if (p.board_in_cannon_events)
        m.Reset("as", character, board);
    else
        m.Reset("a", character);
}

// CannGFTime: `random` for the delay of a real shot, false for the mean the lead estimate uses; `cannon` is the
// cannon's index on its board, -1 for none.
inline void ResetCannonFireTime(MESSAGE &m, const SeaScriptProtocol &p, ATTRIBUTES *character, const std::string &board,
                                bool random, int32_t cannon)
{
    if (p.board_in_cannon_events)
        m.Reset("asll", character, board, random, cannon);
    else
        m.Reset("a", character);
}

// CannGFHeight
inline void ResetCannonFireHeight(MESSAGE &m, const SeaScriptProtocol &p, ATTRIBUTES *character, ATTRIBUTES *enemy,
                                  const std::string &board)
{
    if (p.board_in_cannon_events)
        m.Reset("aas", character, enemy, board);
    else
        m.Reset("aa", character, enemy);
}

// CannDamage
inline void ResetCannonDamage(MESSAGE &m, const SeaScriptProtocol &p, ATTRIBUTES *character, float ballDamage,
                              float cannonDamage, float distance, const glm::vec3 &cannon, const std::string &board)
{
    if (p.board_in_cannon_events)
        m.Reset("affffffs", character, ballDamage, cannonDamage, distance, cannon.x, cannon.y, cannon.z, board);
    else
        m.Reset("affffff", character, ballDamage, cannonDamage, distance, cannon.x, cannon.y, cannon.z);
}

struct CannonShot
{
    glm::vec3 muzzle;
    float speedV0;
    float direction;       // azimuth to the target, radians
    float heightAngle;     // elevation of the shot
    float barrelDirection; // azimuth of the barrel
    float maxDistance;
    float targetAngle; // elevation of the target seen from the muzzle, negative below
};

// CannFire: `automatic` for a shot the engine aimed (the scripts add their dispersion to it).
inline void ResetCannonFire(MESSAGE &m, const SeaScriptProtocol &p, ATTRIBUTES *character, const CannonShot &s,
                            bool automatic, const std::string &board)
{
    if (p.board_in_cannon_events)
        m.Reset("afffffffffls", character, s.muzzle.x, s.muzzle.y, s.muzzle.z, s.speedV0, s.direction, s.heightAngle,
                s.barrelDirection, s.maxDistance, s.targetAngle, automatic, board);
    else
        m.Reset("afffffffff", character, s.muzzle.x, s.muzzle.y, s.muzzle.z, s.speedV0, s.direction, s.heightAngle,
                s.barrelDirection, s.maxDistance, s.targetAngle);
}

// Ball_WaterHit, Ball_IslandHit, Ball_FortHit
inline void ResetBallHit(MESSAGE &m, const SeaScriptProtocol &p, int32_t owner, const glm::vec3 &hit,
                         int32_t cannonType)
{
    if (p.cannon_type_in_ball_events)
        m.Reset("lfffl", owner, hit.x, hit.y, hit.z, cannonType);
    else
        m.Reset("lfff", owner, hit.x, hit.y, hit.z);
}

// Bll_FlyNCam
inline void ResetBallNearCamera(MESSAGE &m, const SeaScriptProtocol &p, const glm::vec3 &ball, int32_t goodIndex)
{
    if (p.cannon_type_in_ball_events)
        m.Reset("fffl", ball.x, ball.y, ball.z, goodIndex);
    else
        m.Reset("fff", ball.x, ball.y, ball.z);
}

struct FortCannonHit
{
    int32_t owner; // the ball owner's character index
    int32_t fort;  // the fort's character index
    ATTRIBUTES *fortLabel;
    int32_t cannons;
    int32_t damagedCannons;
    glm::vec3 cannon;
    float distance;
    float damage; // the cannon's damage so far
};

// FortCDamage
inline void ResetFortCannonDamage(MESSAGE &m, const SeaScriptProtocol &p, const FortCannonHit &h, int32_t cannonType)
{
    if (p.cannon_type_in_ball_events)
        m.Reset("llallfffffl", h.owner, h.fort, h.fortLabel, h.cannons, h.damagedCannons, h.cannon.x, h.cannon.y,
                h.cannon.z, h.distance, h.damage, cannonType);
    else
        m.Reset("llallfffff", h.owner, h.fort, h.fortLabel, h.cannons, h.damagedCannons, h.cannon.x, h.cannon.y,
                h.cannon.z, h.distance, h.damage);
}

// Shp_HullHit
inline void ResetShipHullHit(MESSAGE &m, const SeaScriptProtocol &p, entid_t ship, int32_t owner, int32_t character,
                             const glm::vec3 &hit, int32_t firePlace, float firePlaceDistance, int32_t cannonType)
{
    if (p.cannon_type_in_ball_events)
        m.Reset("illffflfl", ship, owner, character, hit.x, hit.y, hit.z, firePlace, firePlaceDistance, cannonType);
    else
        m.Reset("illffflf", ship, owner, character, hit.x, hit.y, hit.z, firePlace, firePlaceDistance);
}

// ShpSailDamage
inline void ResetSailDamage(MESSAGE &m, const SeaScriptProtocol &p, int32_t character, const glm::vec3 &hit,
                            int32_t owner, int32_t cannonType)
{
    if (p.cannon_type_in_ball_events)
        m.Reset("lfffll", character, hit.x, hit.y, hit.z, owner, cannonType);
    else
        m.Reset("lfffl", character, hit.x, hit.y, hit.z, owner);
}

// ShpMastDamage by a ball (SHIP_MAST_TOUCH_BALL); the island and ship touches are the same in every game.
inline void ResetMastDamageByBall(MESSAGE &m, const SeaScriptProtocol &p, int32_t mast, const glm::vec3 &hit,
                                  float damage, ATTRIBUTES *character, int32_t owner, int32_t cannonType)
{
    if (p.cannon_type_in_ball_events)
        m.Reset("llffffall", int32_t{SHIP_MAST_TOUCH_BALL}, mast, hit.x, hit.y, hit.z, damage, character, owner,
                cannonType);
    else
        m.Reset("llffffal", int32_t{SHIP_MAST_TOUCH_BALL}, mast, hit.x, hit.y, hit.z, damage, character, owner);
}

// ShpHullDamage by a ball (SHIP_HULL_TOUCH_BALL); a hull node whose name contains "baller" is the rudder.
inline void ResetHullDamage(MESSAGE &m, const SeaScriptProtocol &p, int32_t hull, const glm::vec3 &hit, float damage,
                            ATTRIBUTES *character, ATTRIBUTES *ownerCharacter, const std::string &hullName,
                            int32_t cannonType)
{
    switch (p.hull_damage)
    {
    case HullDamageLayout::OwnerHullName:
        m.Reset("llffffaas", int32_t{SHIP_HULL_TOUCH_BALL}, hull, hit.x, hit.y, hit.z, damage, character,
                ownerCharacter, hullName);
        return;
    case HullDamageLayout::HullNameCannonType:
        m.Reset("llffffasl", int32_t{SHIP_HULL_TOUCH_BALL}, hull, hit.x, hit.y, hit.z, damage, character, hullName,
                cannonType);
        return;
    }
}

// Ship_Bort_Fire from an aimed volley (the target point and the direction to it).
inline void ResetBortFire(MESSAGE &m, const SeaScriptProtocol &p, int32_t character, entid_t ship,
                          const std::string &board, const glm::vec3 &target, const glm::vec3 &direction)
{
    if (p.bort_fire_mode)
        m.Reset("lissffffff", character, ship, board, std::string("auto"), target.x, target.y, target.z, direction.x,
                direction.y, direction.z);
    else
        m.Reset("lisffffff", character, ship, board, target.x, target.y, target.z, direction.x, direction.y,
                direction.z);
}

// Shp_GetRunawayPnt
inline void ResetRunawayPoint(MESSAGE &m, const SeaScriptProtocol &p, ATTRIBUTES *character, float x, float z)
{
    if (p.runaway_point_position)
        m.Reset("aff", character, x, z);
    else
        m.Reset("a", character);
}
} // namespace storm::sea_ai
