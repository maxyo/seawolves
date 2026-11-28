// The land fight (location/src/fight_rules.hpp) with fixed script answers of either game: how it reads the answers,
// the layout of the events it reports in each game, and the NPC attack cancel.
#include "fight_rules.hpp"

#include "istring.hpp"

#include <catch2/catch.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

using namespace storm::location;

namespace
{
// Script answers by event name; records every event asked.
struct Scripts
{
    std::map<std::string, float> floats;
    std::map<std::string, int32_t> ints;
    std::vector<std::string> asked;

    AskFloat Float()
    {
        return [this](const char *event) -> std::optional<float> {
            asked.emplace_back(event);
            const auto found = floats.find(event);
            return found == floats.end() ? std::nullopt : std::optional(found->second);
        };
    }
    AskInt Int()
    {
        return [this](const char *event) -> std::optional<int32_t> {
            asked.emplace_back(event);
            const auto found = ints.find(event);
            return found == ints.end() ? std::nullopt : std::optional(found->second);
        };
    }
    bool Asked(const std::string &event) const
    {
        return std::find(asked.begin(), asked.end(), event) != asked.end();
    }
};

const FightRules &rules = fightRules();
} // namespace

TEST_CASE("NPC parameters with To Each His Own's answers: feints, the stun and activities above 1 are kept")
{
    Scripts scripts;
    scripts.floats = {{"NPC_Event_GetAttackWeightFast", 1.0f},  {"NPC_Event_GetAttackWeightForce", 1.0f},
                      {"NPC_Event_GetAttackWeightRound", 1.0f}, {"NPC_Event_GetAttackWeightBreak", 1.0f},
                      {"NPC_Event_GetAttackWeightFeint", 4.0f}, {"NPC_Event_GetAttackActive", 1.1f}};
    scripts.ints = {{"NPC_Event_StunChance", 30}};
    const float teho = landFacts(storm::GameProfile::Legacy).npcActivityMax;
    const auto p = rules.ReadNpcParams(scripts.Float(), scripts.Int(), NpcTuning{}, teho);
    CHECK(p.weightFeint == Approx(0.5f));
    CHECK(p.weightFast == Approx(0.125f));
    CHECK(p.attackActive == Approx(1.1f));
    scripts.floats["NPC_Event_GetAttackActive"] = -0.5f;
    CHECK(rules.ReadNpcParams(scripts.Float(), scripts.Int(), NpcTuning{}, teho).attackActive == 0.0f);
    CHECK(p.stunChance == std::optional<int32_t>(30));
    CHECK_FALSE(p.cancel.enabled); // no handler: no cancels
    const std::vector<std::string> first = {"NPC_Event_GetAttackActive",
                                            "NPC_Event_GetAttackWeightFast",
                                            "NPC_Event_GetAttackWeightForce",
                                            "NPC_Event_GetAttackWeightRound",
                                            "NPC_Event_GetAttackWeightBreak",
                                            "NPC_Event_GetAttackWeightFeint",
                                            "NPC_Event_GetDefenceActive",
                                            "NPC_Event_GetDefenceWeightBlock",
                                            "NPC_Event_GetDefenceWeightParry",
                                            "NPC_Event_EnableRecoil",
                                            "NPC_Event_GetFireActive",
                                            "NPC_Event_EnableFire",
                                            "NPC_Event_StunChance"};
    CHECK(std::vector<std::string>(scripts.asked.begin(), scripts.asked.begin() + first.size()) == first);
}

TEST_CASE("NPC parameters with Caribbean Legend's answers: no feints, cancels with sane reaction ranges")
{
    Scripts scripts;
    scripts.floats = {{"NPC_Event_GetAttackWeightFast", 3.0f},       {"NPC_Event_GetAttackWeightForce", 1.0f},
                      {"NPC_Event_GetAttackWeightRound", 0.0f},      {"NPC_Event_GetAttackWeightBreak", 0.0f},
                      {"NPC_Event_GetCancelDistProb", 0.3f},         {"NPC_Event_GetCancelDistReactionMin", -1.0f},
                      {"NPC_Event_GetCancelDistReactionMax", -2.0f}, {"NPC_Event_GetCancelParryReactionMin", 0.5f},
                      {"NPC_Event_GetCancelParryReactionMax", 0.2f}};
    scripts.ints = {{"NPC_Event_EnableCancel", 1}};
    NpcTuning tuning;
    tuning.cancelParryRefresh = 7.0f;
    scripts.floats["NPC_Event_GetAttackActive"] = 1.1f;
    const auto p = rules.ReadNpcParams(scripts.Float(), scripts.Int(), tuning,
                                       landFacts(storm::GameProfile::CaribbeanLegend).npcActivityMax);
    CHECK(p.attackActive == 1.0f);
    CHECK(p.weightFast == Approx(0.75f));
    CHECK(p.weightFeint == 0.0f);
    CHECK_FALSE(p.stunChance);
    CHECK(p.cancel.enabled);
    CHECK(p.cancel.distProb == Approx(0.3f));
    CHECK(p.cancel.distReactionMin == 0.0f);
    CHECK(p.cancel.distReactionMax == 0.0f);
    CHECK(p.cancel.parryReactionMax == Approx(0.5f));
    CHECK(p.cancel.parryRefresh == 7.0f); // unanswered: the tuning file's default
    Scripts energy;
    CHECK(rules.ActionEnergy(energy.Float()) == Approx(0.1f));
    energy.floats = {{"NPC_Event_GetActionEnergy", 0.4f}};
    CHECK(rules.ActionEnergy(energy.Float()) == Approx(0.4f));
}

TEST_CASE("reach, special strikes and the shot's push follow the answers; no answer changes nothing")
{
    Scripts scripts;
    scripts.floats = {
        {"Event_GetAdditionalMeleeDist", 0.5f}, {"Event_GetAdditionalMeleeAng", 10.0f}, {"Event_GetFireRepulse", 7.0f}};
    scripts.ints = {{"Event_CheckBlockBypass", 1}, {"Event_CheckPowerBreak", 1}, {"Event_CheckPowerRound", 0}};
    const auto reach = rules.Reach({2.4f, 60.0f}, scripts.Float());
    CHECK(reach.distance == Approx(2.9f));
    CHECK(reach.angle == Approx(70.0f));
    CHECK(rules.Special("fast", true, scripts.Int(), scripts.Int()) == SpecialAttack::BlockBypass);
    CHECK(rules.Special("fast", false, scripts.Int(), scripts.Int()) == SpecialAttack::None);
    CHECK(rules.Special("round", true, scripts.Int(), scripts.Int()) == SpecialAttack::None);
    CHECK(rules.Special("break", false, scripts.Int(), scripts.Int()) == SpecialAttack::PowerBreak);
    CHECK(rules.FireRepulse(1.0f, scripts.Float()) == Approx(7.0f));
    CHECK(rules.FireRepulse(14.0f, scripts.Float()) == Approx(3.5f));
    CHECK(rules.FireRepulse(40.0f, scripts.Float()) == Approx(0.0f));

    Scripts quiet;
    const auto same = rules.Reach({2.4f, 60.0f}, quiet.Float());
    CHECK((same.distance == 2.4f && same.angle == 60.0f));
    CHECK(rules.Special("fast", true, quiet.Int(), quiet.Int()) == SpecialAttack::None);
    CHECK(rules.FireRepulse(1.0f, quiet.Float()) == 0.0f);
}

TEST_CASE("a block holds in front, breaks from behind or to a break attack")
{
    Scripts none;
    CHECK_FALSE(rules.Block({false, false, 0.0f}, none.Float()).blocked);
    CHECK(none.asked.empty()); // the back angle is asked for a block only
    const auto front = rules.Block({true, false, 2.0f}, none.Float());
    CHECK((front.blocked && !front.blockBreaks)); // 150 degrees without an answer
    const auto behind = rules.Block({true, false, 2.7f}, none.Float());
    CHECK((!behind.blocked && behind.blockBreaks));
    CHECK(rules.Block({true, true, 0.0f}, none.Float()).blockBreaks);
    Scripts narrow;
    narrow.floats = {{"Event_GetBackBlockAngle", 1.0f}};
    CHECK(rules.Block({true, false, 1.5f}, narrow.Float()).blockBreaks);
}

TEST_CASE("the attack and fire events in each game's layout")
{
    const AttackReport report{7, 9, "force", true, false, std::chrono::milliseconds(1500), 0.25f};
    MESSAGE old;
    rules.ResetAttackEvent(old, LandEventLayout::Legacy, report);
    CHECK(old.GetFormat() == "iislll");
    old.EntityID();
    CHECK(old.EntityID() == 9);
    CHECK(old.String() == "force");
    CHECK(old.Long() == 1);
    CHECK(old.Long() == 0);
    CHECK(old.Long() == 1500);
    MESSAGE unblocked;
    rules.ResetAttackEvent(unblocked, LandEventLayout::Legacy,
                           {7, 9, "fast", false, true, std::chrono::milliseconds(300), 0.0f});
    unblocked.EntityID();
    unblocked.EntityID();
    unblocked.String();
    unblocked.Long();
    CHECK(unblocked.Long() == 1);
    CHECK(unblocked.Long() == -1);

    MESSAGE cl;
    rules.ResetAttackEvent(cl, LandEventLayout::CaribbeanLegend, report);
    CHECK(cl.GetFormat() == "iislffl");
    cl.EntityID();
    cl.EntityID();
    CHECK(cl.String() == "force");
    CHECK(cl.Long() == 1);
    CHECK(cl.Float() == Approx(1.5f));
    CHECK(cl.Float() == Approx(0.25f));
    CHECK(cl.Long() == 0);

    MESSAGE fire, clFire;
    rules.ResetFireEvent(fire, LandEventLayout::Legacy, {7, 9, 0.5f, true});
    CHECK(fire.GetFormat() == "iifl");
    rules.ResetFireEvent(clFire, LandEventLayout::CaribbeanLegend, {7, 9, 0.5f, true});
    CHECK(clFire.GetFormat() == "iiflfl");
    clFire.EntityID();
    clFire.EntityID();
    CHECK(clFire.Float() == 0.5f);
    CHECK(clFire.Long() == 1);
    CHECK(clFire.Float() < 0.0f); // not aimed
}

TEST_CASE("the tuning file sets the constants, or leaves all of them on an error")
{
    NpcTuning tuning;
    CHECK_FALSE(ApplyNpcTuning(R"({"version": 1, "values": {"NPC_CANCEL_BLOCK_DURATION": 0.5, "OTHER": 1,
                                                          "NPC_TACTICAL_ZONE_INNER_MARGIN": 0.75}})",
                               tuning));
    CHECK(tuning.cancelBlockDuration == 0.5f);
    CHECK(tuning.tactical.zoneInnerMargin == 0.75f);
    CHECK(tuning.attackCancelTargetDistance == 2.5f);
    CHECK(ApplyNpcTuning(R"({"version": 2, "values": {"NPC_CANCEL_BLOCK_DURATION": 0.9}})", tuning));
    CHECK(ApplyNpcTuning(R"({"version": 1, "values": {"NPC_ATTACK_CANCEL_TARGET_DISTANCE": 3,
                                                   "NPC_CANCEL_BLOCK_DURATION": "x"}})",
                         tuning));
    CHECK(ApplyNpcTuning("{", tuning));
    CHECK(tuning.cancelBlockDuration == 0.5f);
    CHECK(tuning.attackCancelTargetDistance == 2.5f);
}

TEST_CASE("an NPC cancels a swing at a target out of reach after its reaction time, once a swing")
{
    NpcCancelParams params;
    params.enabled = true;
    params.distProb = 1.0f;
    params.distReactionMin = 0.2f;
    params.distReactionMax = 0.4f;
    const NpcTuning tuning;
    const auto half = [] { return 0.5f; };
    const auto never = [](int32_t) { return 0; };
    AttackCancel cancel;
    const CancelInput far{true, false, true, 3.0f, false, false};
    CHECK(cancel.Update(0.1f, far, params, tuning, half, never) == CancelAction::None); // after 0.3 s
    CHECK(cancel.Update(0.1f, far, params, tuning, half, never) == CancelAction::None);
    CHECK(cancel.Update(0.15f, far, params, tuning, half, never) == CancelAction::Cancel);
    CHECK(cancel.Update(0.5f, far, params, tuning, half, never) == CancelAction::None); // decided for this swing
    const CancelInput idle{false, false, true, 3.0f, false, false};
    cancel.Update(0.1f, idle, params, tuning, half, never);
    CHECK(cancel.Update(0.35f, far, params, tuning, half, never) == CancelAction::Cancel); // the next swing

    AttackCancel close;
    const CancelInput near{true, false, true, 2.0f, false, false};
    for (int i = 0; i < 10; ++i)
        CHECK(close.Update(0.1f, near, params, tuning, half, never) == CancelAction::None);
    params.enabled = false;
    CHECK(AttackCancel{}.Update(1.0f, far, params, tuning, half, never) == CancelAction::None);
}

TEST_CASE("an NPC follows a landed strike with a fast one while the scripts allow, the count resets later")
{
    NpcCancelParams params;
    params.enabled = true;
    const NpcTuning tuning;
    std::vector<int32_t> counts;
    int32_t answer = 1;
    const auto askFast = [&](int32_t count) {
        counts.push_back(count);
        return answer;
    };
    const auto zero = [] { return 0.0f; };
    AttackCancel cancel;
    const CancelInput landed{true, true, true, 1.0f, false, true};
    const CancelInput idle{false, false, true, 1.0f, false, false};
    CHECK(cancel.Update(0.1f, landed, params, tuning, zero, askFast) == CancelAction::CancelFast);
    cancel.Update(0.1f, idle, params, tuning, zero, askFast);
    CHECK(cancel.Update(0.1f, landed, params, tuning, zero, askFast) == CancelAction::CancelFast);
    CHECK(counts == std::vector<int32_t>{1, 2});
    cancel.Update(tuning.cancelFastCounterResetTime + 0.1f, idle, params, tuning, zero, askFast);
    answer = -1; // no energy: the count starts again, asked again next frame
    CHECK(cancel.Update(0.1f, landed, params, tuning, zero, askFast) == CancelAction::None);
    CHECK(cancel.Update(0.1f, landed, params, tuning, zero, askFast) == CancelAction::None);
    CHECK(counts == std::vector<int32_t>{1, 2, 1, 1});
    const CancelInput unharmed{true, true, true, 1.0f, false, false};
    AttackCancel calm;
    CHECK(calm.Update(0.1f, unharmed, params, tuning, zero, askFast) == CancelAction::None);
    CHECK(counts.size() == 4);
}

TEST_CASE("a cancel is rolled against its probability; a parried swing stays decided while the refresh runs")
{
    NpcCancelParams params;
    params.enabled = true;
    params.distProb = 0.4f;
    params.parryProb = 1.0f;
    params.parryReactionMin = params.parryReactionMax = 0.1f;
    params.parryRefresh = 1.0f;
    const NpcTuning tuning;
    const auto never = [](int32_t) { return 0; };
    const auto high = [] { return 0.5f; };
    AttackCancel unlucky;
    const CancelInput far{true, false, true, 3.0f, false, false};
    for (int i = 0; i < 5; ++i)
        CHECK(unlucky.Update(0.2f, far, params, tuning, high, never) == CancelAction::None);

    AttackCancel cancel;
    const CancelInput parried{true, false, true, 1.0f, true, false};
    const CancelInput idle{false, false, true, 1.0f, false, false};
    CHECK(cancel.Update(0.2f, parried, params, tuning, high, never) == CancelAction::Cancel);
    cancel.Update(0.5f, idle, params, tuning, high, never); // 0.5 s of the 1 s refresh
    CHECK(cancel.Update(0.2f, parried, params, tuning, high, never) == CancelAction::None);
    cancel.Update(0.6f, idle, params, tuning, high, never);
    CHECK(cancel.Update(0.2f, parried, params, tuning, high, never) == CancelAction::Cancel);
}

TEST_CASE("no push from a negative answer")
{
    Scripts scripts;
    scripts.floats = {{"Event_GetFireRepulse", -2.0f}};
    CHECK(rules.FireRepulse(1.0f, scripts.Float()) == 0.0f);
}

TEST_CASE("the NPC tuning file of an installed game", "[.corpus]")
{
    namespace fs = std::filesystem;
    const char *root = std::getenv("STORM_GAME_DIR");
    if (!root || !*root)
    {
        std::puts("NOT_RUN: set STORM_GAME_DIR to an installed game");
        std::exit(77);
    }
    fs::path file;
    for (const auto &entry : fs::recursive_directory_iterator(root))
        if (storm::AsciiLower(fs::relative(entry.path(), root).generic_string()) == "resource/ini/npc_ai_tuning.json")
            file = entry.path();
    if (file.empty())
    {
        std::puts("NOT_RUN: this game has no resource/ini/npc_ai_tuning.json");
        std::exit(77);
    }
    std::ifstream in(file, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    NpcTuning tuning;
    const auto error = ApplyNpcTuning(text, tuning);
    CHECK_FALSE(error);
    std::printf("cancel beyond %.2f m, fast counter reset %.2f s, cancel block %.2f s\n",
                tuning.attackCancelTargetDistance, tuning.cancelFastCounterResetTime, tuning.cancelBlockDuration);
}
