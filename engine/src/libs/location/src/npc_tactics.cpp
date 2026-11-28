#include "npc_tactics.hpp"

#include "string_compare.hpp"

#include <cmath>

namespace storm::location
{
namespace
{
float PlaneDistanceSquared(const glm::vec3 &a, const glm::vec3 &b)
{
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return dx * dx + dz * dz;
}

float DistanceSquared(const glm::vec3 &a, const glm::vec3 &b)
{
    const float dy = a.y - b.y;
    return PlaneDistanceSquared(a, b) + dy * dy;
}

// Lower is chosen first.
int TargetPriority(const TargetCandidate &candidate, bool guard)
{
    if (!guard)
        return 3;
    if (candidate.attacksSelf)
        return 0;
    if (candidate.attacksAnchor)
        return 1;
    if (candidate.anchorAttacks)
        return 2;
    return 3;
}
} // namespace

OfficerOrder ParseOfficerOrder(std::string_view order)
{
    if (storm::iEquals(order, "hold"))
        return OfficerOrder::hold;
    if (storm::iEquals(order, "guard"))
        return OfficerOrder::guard;
    return OfficerOrder::attack;
}

bool InOrderZone(const OrderZone &zone, const glm::vec3 &position, float margin)
{
    if (!zone.active)
        return true;
    const float reach = zone.radius + margin;
    return std::fabs(position.y - zone.centre.y) <= zone.height &&
           PlaneDistanceSquared(position, zone.centre) <= reach * reach;
}

std::optional<size_t> ChooseOrderTarget(std::span<const TargetCandidate> candidates, const glm::vec3 &self,
                                        const OrderZone &zone, bool guard, TargetMemory &memory, float dltTime,
                                        const TacticalTuning &tuning)
{
    memory.untilScan -= dltTime;
    if (memory.untilScan > 0.0f)
    {
        if (!memory.id)
            return std::nullopt; // the last scan found no one: wait for the next
        for (size_t i = 0; i < candidates.size(); i++)
            if (candidates[i].id == *memory.id && InOrderZone(zone, candidates[i].position, 0.0f))
                return i;
        // the target is gone or left the zone: scan now
    }
    memory.untilScan = tuning.targetCheckInterval;
    std::optional<size_t> best;
    int bestPriority = 0;
    float bestDistance = 0.0f;
    for (size_t i = 0; i < candidates.size(); i++)
    {
        const TargetCandidate &candidate = candidates[i];
        if (!InOrderZone(zone, candidate.position, 0.0f))
            continue;
        const int priority = TargetPriority(candidate, guard);
        const float distance = DistanceSquared(candidate.position, self);
        if (!best || priority < bestPriority ||
            (priority == bestPriority &&
             (distance < bestDistance || (distance == bestDistance && memory.id == candidate.id))))
        {
            best = i;
            bestPriority = priority;
            bestDistance = distance;
        }
    }
    memory.id = best ? std::optional(candidates[*best].id) : std::nullopt;
    return best;
}

OrderZone ZoneOf(const OrderSituation &situation)
{
    OrderZone zone;
    if (situation.order == OfficerOrder::hold)
        zone.centre = situation.holdPoint;
    else
        zone.centre = situation.anchor.value_or(situation.self);
    zone.radius = situation.radius;
    zone.height = situation.height;
    zone.active = situation.order != OfficerOrder::attack;
    return zone;
}

OrderDecision DecideOrder(const OrderSituation &situation, OrderState &state, const TacticalTuning &tuning)
{
    if (situation.combat != state.combat)
    {
        state.combat = situation.combat;
        state.returning = false;
    }
    const bool hold = situation.order == OfficerOrder::hold;
    if (!situation.combat)
    {
        if (!hold && situation.anchor)
            return {OrderAction::follow, false, "peace: follow"};
        return {OrderAction::stay, false, hold ? "peace: hold" : "peace: follow"};
    }

    const OrderZone zone = ZoneOf(situation);
    // Hysteresis: the officer leaves beyond the outer margin and is back only within the inner one.
    if (zone.active && !InOrderZone(zone, situation.self, tuning.zoneOuterMargin))
        state.returning = true;
    if (state.returning && (!zone.active || InOrderZone(zone, situation.self, -tuning.zoneInnerMargin)))
        state.returning = false;

    if (situation.hasTarget && !state.returning)
        return {OrderAction::fight, true, zone.active ? "target in zone: fight" : "unrestricted attack"};
    const std::string_view label = state.returning ? "outside zone: return"
                                   : zone.active   ? "no target in zone: approach centre/follow"
                                                   : "unrestricted attack";
    if (!hold && !state.returning && situation.anchor)
        return {OrderAction::follow, true, label};
    const float stop = tuning.centerStopDistance;
    if (PlaneDistanceSquared(situation.self, zone.centre) > stop * stop ||
        std::fabs(situation.self.y - zone.centre.y) > zone.height)
        return {OrderAction::moveToCentre, true, label};
    return {OrderAction::stay, true, label};
}

bool NeedOrderMove(bool movingToPoint, const glm::vec3 &goal, const glm::vec3 &centre, float retryTimer,
                   const TacticalTuning &tuning)
{
    if (retryTimer > 0.0f)
        return false;
    return !movingToPoint || DistanceSquared(goal, centre) > tuning.repathDistanceSquared;
}

} // namespace storm::location
