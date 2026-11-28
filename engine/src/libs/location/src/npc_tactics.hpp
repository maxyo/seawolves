// The officer orders a game's scripts give through `chr_ai.tmpl.tacticalOrder` (guard a character, attack, hold a
// place): pure decisions over what the officer sees, applied by NPCharacter through its tasks. A game whose scripts
// never set the attribute never reaches this code.
#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace storm::location
{

// The NPC_TACTICAL_* values of resource/ini/npc_ai_tuning.json; the defaults are the engine values of the game that
// ships the file.
struct TacticalTuning
{
    float defaultZoneRadius = 7.0f;          // NPC_TACTICAL_DEFAULT_ZONE_RADIUS, m: an order without a radius
    float defaultZoneHeight = 1.5f;          // NPC_TACTICAL_DEFAULT_ZONE_HEIGHT, m: an order without a height
    float zoneOuterMargin = 0.35f;           // NPC_TACTICAL_ZONE_OUTER_MARGIN, m: beyond radius + this, return
    float zoneInnerMargin = 0.5f;            // NPC_TACTICAL_ZONE_INNER_MARGIN, m: back within radius - this
    float centerStopDistance = 0.8f;         // NPC_TACTICAL_CENTER_STOP_DISTANCE, m: no move closer to the centre
    float returnStopDistance = 0.6f;         // NPC_TACTICAL_RETURN_STOP_DISTANCE, m: the move to the centre stops
    float moveRetryTime = 1.0f;              // NPC_TACTICAL_MOVE_RETRY_TIME, s: after a move that could not start
    float repathDistanceSquared = 1.0f;      // NPC_TACTICAL_REPATH_DISTANCE_SQUARED, m^2: the centre moved this far
    float escapeStartDistanceSquared = 1.0f; // NPC_TACTICAL_ESCAPE_START_DISTANCE_SQUARED, m^2
    float targetCheckInterval = 0.2f;        // NPC_TACTICAL_TARGET_CHECK_INTERVAL, s: between target scans
};

// The order's string: "hold" and "guard" by name, anything else attacks.
enum class OfficerOrder
{
    hold,
    guard,
    attack
};

OfficerOrder ParseOfficerOrder(std::string_view order);

// Where a guard or hold order keeps the officer while there is a fight (in peace the zone is not asked). An attack
// order has none.
struct OrderZone
{
    glm::vec3 centre{};
    float radius = 0.0f;
    float height = 0.0f;
    bool active = false;
};

// Inside the zone on the plane (radius + margin) and on its level; an inactive zone holds everything.
bool InOrderZone(const OrderZone &zone, const glm::vec3 &position, float margin);

// An enemy the officer may choose: a living character of a hostile group in fight mode.
struct TargetCandidate
{
    int64_t id = 0;
    glm::vec3 position{};
    bool attacksSelf = false;   // its fight is aimed at the officer
    bool attacksAnchor = false; // its fight is aimed at the guarded character
    bool anchorAttacks = false; // the guarded character's fight is aimed at it
};

// The officer's target between scans: kept while it is still a candidate inside the zone, a new scan when the
// interval runs out or the target is lost.
struct TargetMemory
{
    std::optional<int64_t> id;
    float untilScan = 0.0f;
};

// The index of the chosen candidate. A guard order prefers who attacks the officer, then who attacks the guarded
// character, then whom the guarded character fights; then the nearest, the previous target on a tie.
std::optional<size_t> ChooseOrderTarget(std::span<const TargetCandidate> candidates, const glm::vec3 &self,
                                        const OrderZone &zone, bool guard, TargetMemory &memory, float dltTime,
                                        const TacticalTuning &tuning);

// What the officer knows this frame.
struct OrderSituation
{
    OfficerOrder order = OfficerOrder::guard;
    bool combat = false; // the scripts' answer: a fight the officer takes part in
    glm::vec3 self{};
    std::optional<glm::vec3> anchor; // the guarded character, else the player
    glm::vec3 holdPoint{};           // the place of a hold order
    float radius = 0.0f;
    float height = 0.0f;
    bool hasTarget = false; // ChooseOrderTarget found one
};

// The officer's memory between frames.
struct OrderState
{
    bool combat = false;
    bool returning = false; // left the zone: goes back before fighting again
};

enum class OrderAction
{
    follow,       // follow the anchor
    fight,        // fight the chosen target
    moveToCentre, // run to the zone's centre
    stay
};

struct OrderDecision
{
    OrderAction action = OrderAction::stay;
    bool fightMode = false;
    std::string_view label; // for the debug view
};

OrderZone ZoneOf(const OrderSituation &situation);

OrderDecision DecideOrder(const OrderSituation &situation, OrderState &state, const TacticalTuning &tuning);

// Whether the move to the centre is issued again: not while a failed start waits out its retry time, and not while
// the running move already heads to within the repath distance of the centre.
bool NeedOrderMove(bool movingToPoint, const glm::vec3 &goal, const glm::vec3 &centre, float retryTimer,
                   const TacticalTuning &tuning);

} // namespace storm::location
