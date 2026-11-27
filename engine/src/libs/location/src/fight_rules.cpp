#include "fight_rules.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <string>

namespace storm::location
{

std::optional<std::string> ApplyNpcTuning(std::string_view json, NpcTuning &tuning)
{
    const auto root = nlohmann::json::parse(json, nullptr, false);
    if (!root.is_object())
        return "not a JSON object";
    if (root.value("version", 0) != 1)
        return "version is not 1";
    const auto values = root.find("values");
    if (values == root.end() || !values->is_object())
        return "no \"values\" object";
    const std::map<std::string_view, float NpcTuning::*> names = {
        {"NPC_ATTACK_CANCEL_TARGET_DISTANCE", &NpcTuning::attackCancelTargetDistance},
        {"NPC_CANCEL_FAST_COUNTER_RESET_TIME", &NpcTuning::cancelFastCounterResetTime},
        {"NPC_CANCEL_BLOCK_DURATION", &NpcTuning::cancelBlockDuration},
        {"NPC_DEFAULT_CANCEL_DIST_REACTION_MIN", &NpcTuning::cancelDistReactionMin},
        {"NPC_DEFAULT_CANCEL_DIST_REACTION_MAX", &NpcTuning::cancelDistReactionMax},
        {"NPC_DEFAULT_CANCEL_PARRY_REACTION_MIN", &NpcTuning::cancelParryReactionMin},
        {"NPC_DEFAULT_CANCEL_PARRY_REACTION_MAX", &NpcTuning::cancelParryReactionMax},
        {"NPC_DEFAULT_CANCEL_PARRY_REFRESH", &NpcTuning::cancelParryRefresh},
    };
    NpcTuning read = tuning;
    for (const auto &[name, member] : names)
    {
        const auto value = values->find(name);
        if (value == values->end())
            continue; // the engine keeps its value, as for a name the file leaves out
        if (!value->is_number())
            return std::string(name) + " is not a number";
        read.*member = value->get<float>();
    }
    tuning = read;
    return std::nullopt;
}

namespace
{
template <typename T> void Take(T &value, const std::optional<T> &answer)
{
    if (answer)
        value = *answer;
}

void Normalise(std::initializer_list<float *> weights)
{
    float sum = 0.0f;
    for (const float *weight : weights)
        sum += *weight;
    if (sum > 0.0f)
    {
        const float scale = 1.0f / sum;
        for (float *weight : weights)
            *weight *= scale;
    }
}

// The asks both models make, in their order: attack, then (after the legacy feint) defence and fire.
void ReadAttackParams(NpcFightParams &p, const AskFloat &askFloat)
{
    Take(p.attackActive, askFloat("NPC_Event_GetAttackActive"));
    Take(p.weightFast, askFloat("NPC_Event_GetAttackWeightFast"));
    Take(p.weightForce, askFloat("NPC_Event_GetAttackWeightForce"));
    Take(p.weightRound, askFloat("NPC_Event_GetAttackWeightRound"));
    Take(p.weightBreak, askFloat("NPC_Event_GetAttackWeightBreak"));
}

void ReadDefenceAndFireParams(NpcFightParams &p, const AskFloat &askFloat, const AskInt &askInt)
{
    Take(p.defenceActive, askFloat("NPC_Event_GetDefenceActive"));
    Take(p.weightBlock, askFloat("NPC_Event_GetDefenceWeightBlock"));
    Take(p.weightParry, askFloat("NPC_Event_GetDefenceWeightParry"));
    if (const auto recoil = askInt("NPC_Event_EnableRecoil"))
        p.recoilEnabled = *recoil != 0;
    Take(p.fireActive, askFloat("NPC_Event_GetFireActive"));
    if (const auto fire = askInt("NPC_Event_EnableFire"))
        p.fireEnabled = *fire != 0;
}

class LegacyRules final : public FightRules
{
  public:
    NpcFightParams ReadNpcParams(const AskFloat &askFloat, const AskInt &askInt, const NpcTuning &) const override
    {
        NpcFightParams p;
        ReadAttackParams(p, askFloat);
        Take(p.weightFeint, askFloat("NPC_Event_GetAttackWeightFeint"));
        ReadDefenceAndFireParams(p, askFloat, askInt);
        p.stunChance = askInt("NPC_Event_StunChance");
        p.attackActive = std::clamp(p.attackActive, 0.0f, 1000.0f);
        p.defenceActive = std::clamp(p.defenceActive, 0.0f, 1000.0f);
        Normalise({&p.weightFast, &p.weightForce, &p.weightRound, &p.weightBreak, &p.weightFeint});
        Normalise({&p.weightBlock, &p.weightParry});
        return p;
    }

    float ActionEnergy(const AskFloat &ask) const override
    {
        return ask("NPC_Event_GetActionEnergy").value_or(0.1f);
    }

    MeleeReach Reach(MeleeReach animation, const AskFloat &) const override
    {
        return animation;
    }

    SpecialAttack Special(std::string_view, bool, const AskInt &, const AskInt &) const override
    {
        return SpecialAttack::None;
    }

    float FireRepulse(float, const AskFloat &) const override
    {
        return 0.0f;
    }

    BlockOutcome Block(const BlockInput &input, const AskFloat &) const override
    {
        return {input.victimBlocking && !input.breakAttack, false};
    }

    void ResetAttackEvent(MESSAGE &message, const AttackReport &r) const override
    {
        // whole milliseconds the block was held, -1 when the strike was not blocked
        const int32_t blockMs =
            r.blocked && r.blockHeld
                ? static_cast<int32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(*r.blockHeld).count())
                : -1;
        message.Reset("iislll", r.attacker, r.victim, std::string(r.type), static_cast<int32_t>(r.blocked),
                      static_cast<int32_t>(r.gunBlade), blockMs);
    }

    void ResetFireEvent(MESSAGE &message, const FireReport &r) const override
    {
        message.Reset("iifl", r.attacker, r.enemy, r.kDist, static_cast<int32_t>(r.hit));
    }
};

class CaribbeanLegendRules final : public FightRules
{
  public:
    NpcFightParams ReadNpcParams(const AskFloat &askFloat, const AskInt &askInt, const NpcTuning &tuning) const override
    {
        NpcFightParams p;
        p.weightFeint = 0.0f;
        p.cancel.distReactionMin = tuning.cancelDistReactionMin;
        p.cancel.distReactionMax = tuning.cancelDistReactionMax;
        p.cancel.parryReactionMin = tuning.cancelParryReactionMin;
        p.cancel.parryReactionMax = tuning.cancelParryReactionMax;
        p.cancel.parryRefresh = tuning.cancelParryRefresh;
        ReadAttackParams(p, askFloat);
        ReadDefenceAndFireParams(p, askFloat, askInt);
        if (const auto enabled = askInt("NPC_Event_EnableCancel"))
            p.cancel.enabled = *enabled != 0;
        auto &c = p.cancel;
        Take(c.distProb, askFloat("NPC_Event_GetCancelDistProb"));
        Take(c.parryProb, askFloat("NPC_Event_GetCancelParryProb"));
        Take(c.distReactionMin, askFloat("NPC_Event_GetCancelDistReactionMin"));
        Take(c.distReactionMax, askFloat("NPC_Event_GetCancelDistReactionMax"));
        Take(c.parryReactionMin, askFloat("NPC_Event_GetCancelParryReactionMin"));
        Take(c.parryReactionMax, askFloat("NPC_Event_GetCancelParryReactionMax"));
        Take(c.parryRefresh, askFloat("NPC_Event_GetCancelParryRefresh"));
        c.distReactionMin = std::max(c.distReactionMin, 0.0f);
        c.distReactionMax = std::max(c.distReactionMax, c.distReactionMin);
        c.parryReactionMin = std::max(c.parryReactionMin, 0.0f);
        c.parryReactionMax = std::max(c.parryReactionMax, c.parryReactionMin);
        p.attackActive = std::clamp(p.attackActive, 0.0f, 1.0f);
        p.defenceActive = std::clamp(p.defenceActive, 0.0f, 1.0f);
        Normalise({&p.weightFast, &p.weightForce, &p.weightRound, &p.weightBreak});
        Normalise({&p.weightBlock, &p.weightParry});
        return p;
    }

    // The game has no such event: its scripts check the energy themselves.
    float ActionEnergy(const AskFloat &) const override
    {
        return 0.1f;
    }

    SpecialAttack Special(std::string_view type, bool targetBlocks, const AskInt &askTarget,
                          const AskInt &askSelf) const override
    {
        const auto yes = [](const std::optional<int32_t> &answer) { return answer.value_or(0) > 0; };
        if ((type == "fast" || type == "force") && targetBlocks && yes(askTarget("Event_CheckBlockBypass")))
            return SpecialAttack::BlockBypass;
        if (type == "break" && yes(askSelf("Event_CheckPowerBreak")))
            return SpecialAttack::PowerBreak;
        if (type == "round" && yes(askSelf("Event_CheckPowerRound")))
            return SpecialAttack::PowerRound;
        return SpecialAttack::None;
    }

    // The push fades from full at 3 m to none at 25 m.
    float FireRepulse(float distance, const AskFloat &ask) const override
    {
        const float power = ask("Event_GetFireRepulse").value_or(0.0f);
        if (power <= 0.0f)
            return 0.0f;
        const float d = std::clamp(distance, 3.0f, 25.0f);
        return power * (1.0f - (d - 3.0f) / 22.0f);
    }

    MeleeReach Reach(MeleeReach animation, const AskFloat &ask) const override
    {
        return {animation.distance + ask("Event_GetAdditionalMeleeDist").value_or(0.0f),
                animation.angle + ask("Event_GetAdditionalMeleeAng").value_or(0.0f)};
    }

    BlockOutcome Block(const BlockInput &input, const AskFloat &backAngle) const override
    {
        if (!input.victimBlocking)
            return {false, false};
        constexpr float kBackAngle = 2.6179939f; // 150 degrees: a block holds within this of the facing
        if (input.defenceAngle > backAngle("Event_GetBackBlockAngle").value_or(kBackAngle))
            return {false, true};
        if (input.breakAttack)
            return {false, true};
        return {true, false};
    }

    void ResetAttackEvent(MESSAGE &message, const AttackReport &r) const override
    {
        // The game's strike that lands on a block without breaking it ("mini strike") is not known to this
        // engine: it reports none.
        message.Reset("iislffl", r.attacker, r.victim, std::string(r.type), static_cast<int32_t>(r.blocked),
                      r.blockHeld ? std::chrono::duration<float>(*r.blockHeld).count() : 0.0f, r.defenceAngle,
                      int32_t{0});
    }

    void ResetFireEvent(MESSAGE &message, const FireReport &r) const override
    {
        // aiming time -1: the shot was not aimed (this engine has no aiming); no head shots
        message.Reset("iiflfl", r.attacker, r.enemy, r.kDist, static_cast<int32_t>(r.hit), -1.0f, int32_t{0});
    }
};
} // namespace

const FightRules &fightRules(FightModel model)
{
    static const LegacyRules legacy;
    static const CaribbeanLegendRules caribbeanLegend;
    return model == FightModel::CaribbeanLegend ? static_cast<const FightRules &>(caribbeanLegend) : legacy;
}

CancelAction AttackCancel::Update(float dt, const CancelInput &in, const NpcCancelParams &params,
                                  const NpcTuning &tuning, const std::function<float()> &uniform01,
                                  const std::function<int32_t(int32_t)> &askFast)
{
    if (!params.enabled)
        return CancelAction::None;
    if (fastCooldown_ > 0.0f && (fastCooldown_ -= dt) <= 0.0f)
        fastCount_ = 0;
    if (!in.hasTarget || !in.attacking)
    {
        scheduled_ = -1.0f;
        if (parryRefresh_ > 0.0f)
            parryRefresh_ -= dt;
        if (parryRefresh_ <= 0.0f)
            decided_ = false;
        return CancelAction::None;
    }
    const auto schedule = [&](float probability, float min, float max) {
        decided_ = true;
        if (uniform01() < probability)
            scheduled_ = min + uniform01() * (max - min);
    };
    if (!decided_)
    {
        if (!in.hitDone)
        {
            if (in.targetDistance > tuning.attackCancelTargetDistance)
                schedule(params.distProb, params.distReactionMin, params.distReactionMax);
            else if (in.targetParries)
            {
                schedule(params.parryProb, params.parryReactionMin, params.parryReactionMax);
                parryRefresh_ = params.parryRefresh;
            }
        }
        else if (in.targetStaggered)
        {
            switch (askFast(++fastCount_))
            {
            case -1:
                fastCount_ = 0;
                break;
            case 1:
                decided_ = true;
                fastCooldown_ = tuning.cancelFastCounterResetTime;
                return CancelAction::CancelFast;
            default:
                decided_ = true;
                break;
            }
        }
    }
    if (scheduled_ >= 0.0f && (scheduled_ -= dt) <= 0.0f)
    {
        scheduled_ = -1.0f;
        return CancelAction::Cancel;
    }
    return CancelAction::None;
}

} // namespace storm::location
