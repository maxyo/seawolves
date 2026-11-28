// The character commands of Caribbean Legend's interaction sessions (character_interaction.hpp).
#include "character.h"
#include "land_protocol.hpp"
#include "location.h"

#include "core.h"
#include "string_compare.hpp"
#include "v_s_stack.h"

#include <array>
#include <cmath>
#include <string_view>

namespace
{
// A spot's floor may be this far above or below it (Caribbean Legend's patch projection).
constexpr float kFloorTolerance = 0.25f;

// Commands of the same family that move the character: they wait for the movement layer, and are refused by name.
constexpr std::array<std::string_view, 6> kNotYet = {"BlendToPosition",    "BlendAnimationToPosition",
                                                     "PositionBlendState", "CancelPositionBlend",
                                                     "BlendNextAction",    "IsMovementStopped"};
constexpr std::string_view kWithoutBlends =
    "position blends of characters: a character does not walk onto a seat or stand up from it";
} // namespace

bool Character::IsInInteractionSession() const
{
    return interaction_.SessionActive();
}

std::optional<uint32_t> Character::zExMessageInteraction(const std::string &msg, MESSAGE &message)
{
    using storm::iEquals;
    // The commands that place the character need its model (Caribbean Legend refuses them without one).
    const bool hasModel = Model() != nullptr;
    if (iEquals(msg, "BeginInteractionSession"))
    {
        message.AttributePointer(); // the script's session record: the engine keeps nothing of it
        message.Long();             // whether it is the player's: nothing of the engine depends on it
        return hasModel ? interaction_.Begin() : 0;
    }
    if (iEquals(msg, "InteractionTransition"))
    {
        const auto token = static_cast<uint32_t>(message.Long());
        return interaction_.Transition(token, message.Long() != 0) ? 1 : 0;
    }
    if (iEquals(msg, "IsInteractionTransitionLocked"))
        return interaction_.TransitionLocked() ? 1 : 0;
    if (iEquals(msg, "EndInteractionSession"))
        return interaction_.End(static_cast<uint32_t>(message.Long())) ? 1 : 0;
    if (iEquals(msg, "LockHeight"))
    {
        const float height = message.Float();
        return hasModel ? interaction_.LockHeight(height) : 0;
    }
    if (iEquals(msg, "UnlockHeight"))
    {
        interaction_.UnlockHeight(static_cast<uint32_t>(message.Long()));
        return 1;
    }
    if (iEquals(msg, "FindFreePositionNear"))
    {
        glm::vec3 center;
        center.x = message.Float();
        center.y = message.Float();
        center.z = message.Float();
        const float searchRadius = message.Float();
        VDATA *outX = message.ScriptVariablePointer();
        VDATA *outY = message.ScriptVariablePointer();
        VDATA *outZ = message.ScriptVariablePointer();
        const bool ignoreCharacters = message.Long() != 0;
        Location *location = GetLocation();
        if (!hasModel || location == nullptr || !outX || !outY || !outZ)
            return 0;
        PtcData &patch = location->GetPtcData();
        // On the floor within the tolerance, the floor also under the character's half radius around the spot, and
        // (unless ignored) no other character there.
        const auto onFloor = [&](glm::vec3 &spot) {
            float floor;
            if (patch.FindNode(spot, floor) < 0 || std::fabs(floor - spot.y) > kFloorTolerance)
                return false;
            spot.y = floor;
            return true;
        };
        const float clearance = radius * 0.5f;
        const auto isFree = [&](glm::vec3 &spot) {
            if (!onFloor(spot))
                return false;
            for (const glm::vec3 offset : {glm::vec3(clearance, 0, 0), glm::vec3(-clearance, 0, 0),
                                           glm::vec3(0, 0, clearance), glm::vec3(0, 0, -clearance)})
            {
                glm::vec3 edge = spot + offset;
                if (!onFloor(edge))
                    return false;
            }
            return ignoreCharacters || location->supervisor.CheckPosition(spot.x, spot.y, spot.z, this);
        };
        const auto found = storm::location::FindFreePositionNear(center, searchRadius, curPos, isFree);
        if (!found)
            return 0;
        outX->Set(found->x);
        outY->Set(found->y);
        outZ->Set(found->z);
        return 1;
    }
    for (const std::string_view name : kNotYet)
        if (iEquals(msg, name))
        {
            storm::location::ReportNotImplemented(fmt::format("character command '{}'", name), kWithoutBlends);
            return 0;
        }
    return std::nullopt;
}
