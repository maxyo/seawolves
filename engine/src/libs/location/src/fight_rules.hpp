// The parts of land fighting that differ between the games' fight models (land_protocol.hpp FightModel): what the
// engine asks the scripts, how it reads the answers and what it reports. One FightRules per model, chosen once;
// the asks are callbacks so the decisions run with fixed answers in the host checks.
#pragma once

#include "land_protocol.hpp"
#include "message.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace storm::location
{

// An event the engine raises for an answer: nullopt when no handler answered.
using AskFloat = std::function<std::optional<float>(const char *event)>;
using AskInt = std::function<std::optional<int32_t>(const char *event)>;

// The tunable constants of the NPC fight that Caribbean Legend reads from resource/ini/npc_ai_tuning.json
// ({"version": 1, "values": {NAME: number}}); the defaults are that file's values.
struct NpcTuning
{
    float attackCancelTargetDistance = 2.5f; // NPC_ATTACK_CANCEL_TARGET_DISTANCE, m
    float cancelFastCounterResetTime = 4.0f; // NPC_CANCEL_FAST_COUNTER_RESET_TIME, s
    float cancelBlockDuration = 0.2f;        // NPC_CANCEL_BLOCK_DURATION, s
    float cancelDistReactionMin = 0.1f;      // NPC_DEFAULT_CANCEL_DIST_REACTION_MIN, s
    float cancelDistReactionMax = 0.4f;      // NPC_DEFAULT_CANCEL_DIST_REACTION_MAX, s
    float cancelParryReactionMin = 0.1f;     // NPC_DEFAULT_CANCEL_PARRY_REACTION_MIN, s
    float cancelParryReactionMax = 0.4f;     // NPC_DEFAULT_CANCEL_PARRY_REACTION_MAX, s
    float cancelParryRefresh = 2.0f;         // NPC_DEFAULT_CANCEL_PARRY_REFRESH, s
};

// Applies the values of the file to `tuning`; on an error returns it and leaves `tuning` as it was.
std::optional<std::string> ApplyNpcTuning(std::string_view json, NpcTuning &tuning);

struct NpcCancelParams
{
    bool enabled = false;
    float distProb = 0.0f, parryProb = 0.0f;
    float distReactionMin = 0.0f, distReactionMax = 0.0f;
    float parryReactionMin = 0.0f, parryReactionMax = 0.0f;
    float parryRefresh = 0.0f;
};

// What an NPC's fight AI is given by the scripts (NPC init and MSG_NPCHARACTER_FIGHTPROBABILITIES_UPDATE).
struct NpcFightParams
{
    float attackActive = 0.5f;
    float weightFast = 0.2f, weightForce = 0.5f, weightRound = 0.1f, weightBreak = 0.1f, weightFeint = 0.1f;
    float defenceActive = 0.05f;
    float weightBlock = 0.9f, weightParry = 0.1f;
    bool recoilEnabled = true;
    float fireActive = 0.0f;
    bool fireEnabled = true;
    std::optional<int32_t> stunChance; // asked by the legacy model only
    NpcCancelParams cancel;            // asked by Caribbean Legend's only
};

struct MeleeReach
{
    float distance; // m
    float angle;    // degrees, as the animation's "Ang"
};

// A victim of a melee strike as the hit resolution sees it.
struct BlockInput
{
    bool victimBlocking; // block or block hit
    bool breakAttack;    // a break attack
    float defenceAngle;  // radians in [0, pi] between the victim's facing and the attacker
};

struct BlockOutcome
{
    bool blocked;
    bool blockBreaks; // the victim reacts with a broken block instead of the attack's reaction
};

// One victim hit, for the Location_CharacterAttack event.
struct AttackReport
{
    entid_t attacker, victim;
    const char *type; // "fast", "force", "round", "break", "feint"
    bool blocked;
    bool gunBlade;                                                // the legacy sabergun strike
    std::optional<std::chrono::steady_clock::duration> blockHeld; // since the victim raised its block
    float defenceAngle;                                           // radians
};

// A shot, for the Location_CharacterFire event.
struct FireReport
{
    entid_t attacker, enemy;
    float kDist;
    bool hit;
};

// A strike played from a group of its own (the character's actions attack_block_bypass, attack_power_break,
// attack_power_round) instead of the attack's.
enum class SpecialAttack
{
    None,
    BlockBypass,
    PowerBreak,
    PowerRound,
};

class FightRules
{
  public:
    virtual ~FightRules() = default;
    virtual NpcFightParams ReadNpcParams(const AskFloat &askFloat, const AskInt &askInt,
                                         const NpcTuning &tuning) const = 0;
    // The energy an NPC needs for an action, for its choice between parry and block.
    virtual float ActionEnergy(const AskFloat &ask) const = 0;
    // Distance and angle a strike reaches from the animation's own; `ask` raises an event with the attack type.
    virtual MeleeReach Reach(MeleeReach animation, const AskFloat &ask) const = 0;
    // An attack the scripts allowed: `askTarget` raises an event with the attacker, the target and the type,
    // `askSelf` with the attacker alone.
    virtual SpecialAttack Special(std::string_view type, bool targetBlocks, const AskInt &askTarget,
                                  const AskInt &askSelf) const = 0;
    // The speed a hit by a shot pushes the victim away with at `distance` metres; `ask` raises an event with the
    // shooter and the victim.
    virtual float FireRepulse(float distance, const AskFloat &ask) const = 0;
    // `backAngle`: asked only for a victim whose block may hold.
    virtual BlockOutcome Block(const BlockInput &input, const AskFloat &backAngle) const = 0;
    virtual void ResetAttackEvent(MESSAGE &message, const AttackReport &report) const = 0;
    virtual void ResetFireEvent(MESSAGE &message, const FireReport &report) const = 0;
};

const FightRules &fightRules(FightModel model);

inline const FightRules &fightRules()
{
    return fightRules(landFacts().fight);
}

// Caribbean Legend's NPC attack cancel, run each frame of an NPC's fight (the legacy model has none).
struct CancelInput
{
    bool attacking; // the NPC's current action is an attack
    bool hitDone;   // its strike has already landed in this swing
    bool hasTarget; // a live target
    float targetDistance;
    bool targetParries;
    bool targetStaggered; // hit, hit by a feint, parry, round or shot, or a broken block
};

enum class CancelAction
{
    None,
    Cancel,     // NPC_Event_DoCancelAttack(""), stop the attack, block for cancelBlockDuration
    CancelFast, // NPC_Event_DoCancelAttack("fast"), stop the attack, strike fast at once
};

class AttackCancel
{
  public:
    // `uniform01`: a random number in [0, 1); `askFast(count)`: NPC_Event_GetCancelFast, -1 no energy, 0 no,
    // 1 cancel into a fast strike.
    CancelAction Update(float dt, const CancelInput &input, const NpcCancelParams &params, const NpcTuning &tuning,
                        const std::function<float()> &uniform01, const std::function<int32_t(int32_t)> &askFast);

  private:
    bool decided_ = false;
    float scheduled_ = -1.0f; // seconds until a cancel; negative: none
    float parryRefresh_ = 0.0f;
    float fastCooldown_ = 0.0f;
    int32_t fastCount_ = 0;
};

} // namespace storm::location
