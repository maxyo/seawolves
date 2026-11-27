#include "steam_api_script_lib.hpp"

#include "core.h"
#include "game_profile.hpp"
#include "s_import_func.h"
#include "v_s_stack.h"
#include "vma.hpp"

#ifndef __ANDROID__
#include "platform/desktop_settings.hpp"
#endif

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <set>
#include <string>
#include <vector>

// The game scripts call the Steam functions of the original PC release (achievements, stats, DLC
// checks). There is no Steam on our platforms: the functions keep their script signatures and
// report "no Steam", no achievements/stats; the add-ons and the player's name are what the game's profile and the
// player's settings say (GetDLCenabled, GetPersonaName).
namespace steamapi
{
CREATE_SCRIPTLIBRIARY(SteamApiScriptLib)

namespace
{
// What the player says of himself: desktop settings.json (platform/desktop_settings.hpp); Android has no such settings.
std::string playerName;
std::vector<int32_t> ownedApps;

void ReadPlayerSettings()
{
#ifdef __ANDROID__
    spdlog::info("player settings: the app has no player name or owned add-ons; none");
#else
    const auto file = storm::desktop::settingsFile(storm::desktop::baseFolders(std::getenv));
    try
    {
        auto settings = storm::desktop::playerSettings(file);
        spdlog::info("player settings: name {}, owned store apps {}", settings.name.empty() ? "unset" : "set",
                     settings.ownedApps.size());
        playerName = std::move(settings.name);
        ownedApps = std::move(settings.ownedApps);
    }
    catch (const std::exception &error)
    {
        spdlog::error("player settings: {}; no name and no owned store apps", error.what());
    }
#endif
}

// `add_on`: GetDLCenabled, which the game's profile may answer for every add-on; else BIsSubscribedApp.
template <bool add_on> uint32_t Owned(VS_STACK *stack)
{
    const int32_t id = static_cast<VDATA *>(stack->Pop())->GetInt();
    const bool owned = (add_on && storm::gameProfileFacts().dlc_published_with_game) ||
                       std::ranges::find(ownedApps, id) != ownedApps.end();
    static std::set<int32_t> logged;
    if (logged.insert(id).second)
        spdlog::info("store app {}: {}", id, owned ? "owned" : "not owned");
    static_cast<VDATA *>(stack->Push())->Set(int32_t{owned});
    return IFUNCRESULT_OK;
}

uint32_t GetPersonaName(VS_STACK *stack)
{
    static_cast<VDATA *>(stack->Push())->Set(playerName.c_str());
    return IFUNCRESULT_OK;
}

// Caribbean Legend's purchase check: its own engine answers IsVerified true and does nothing in Verify, so the
// scripts' Verifier_* chain never ends the game; ownership is checked by Steam when that engine starts. There is no
// Steam here and nothing is checked: these natives only keep the scripts' calls valid.
uint32_t IsVerified(VS_STACK *stack)
{
    static_cast<VDATA *>(stack->Push())->Set(int32_t{1});
    return IFUNCRESULT_OK;
}

uint32_t Verify(VS_STACK *)
{
    return IFUNCRESULT_OK;
}

// Pops `arguments` script arguments and pushes `result`.
template <int32_t result, int arguments> uint32_t answer(VS_STACK *pS)
{
    for (int i = 0; i < arguments; ++i)
    {
        if (!pS->Pop())
        {
            return IFUNCRESULT_FAILED;
        }
    }
    auto *pV = pS->Push();
    if (!pV)
    {
        return IFUNCRESULT_FAILED;
    }
    pV->Set(result);
    return IFUNCRESULT_OK;
}

struct ScriptFunction
{
    const char *name;
    int32_t arguments;
    uint32_t (*function)(VS_STACK *);
};

constexpr ScriptFunction kFunctions[] = {
    {"GetSteamEnabled", 0, answer<0, 0>},  {"GetDLCenabled", 1, Owned<true>},
    {"GetDLCCount", 0, answer<0, 0>},      {"GetDLCData", 1, answer<0, 1>},
    {"DLCStartOverlay", 1, answer<0, 1>},  {"SetAchievement", 1, answer<0, 1>},
    {"GetAchievement", 1, answer<0, 1>},   {"SetStat", 2, answer<0, 2>},
    {"GetStat", 1, answer<0, 1>},          {"StoreStats", 0, answer<0, 0>},
    {"ClearAchievement", 1, answer<0, 1>}, {"ResetStats", 1, answer<0, 1>},
    // Caribbean Legend's Steam account, overlay, rich presence, Workshop, DLC and curator queries: no Steam client,
    // no overlay, no Workshop items; curator status -1 is the scripts' "not available".
    {"GetSteamID", 0, answer<0, 0>},       {"GetAppID", 0, answer<0, 0>},
    {"IsSteamDeck", 0, answer<0, 0>},      {"IsVirtualKeyboardShowing", 0, answer<0, 0>},
    {"ShowSteamVirtualKeyboard", 5, answer<0, 5>}, {"GetOverlaysInfo", 1, answer<0, 1>},
    {"GameOverlayToWebPage", 1, answer<0, 1>}, {"GameOverlayToWebPageModal", 1, answer<0, 1>},
    {"SteamSetRichPresence", 2, answer<0, 2>}, {"SteamClearRichPresence", 0, answer<0, 0>},
    {"BIsSubscribedApp", 1, Owned<false>}, {"BeginCuratorCheckAsync", 2, answer<-1, 2>},
    {"IsVerified", 0, IsVerified},
};
} // namespace

// Caribbean Legend's Discord rich presence (#libriary "DISCORD_SCRIPT_LIBRARY"): no Discord client.
class DISCORD_SCRIPT_LIBRARY final : public SCRIPT_LIBRIARY
{
  public:
    bool Init() override;
};
CREATE_SCRIPTLIBRIARY(DISCORD_SCRIPT_LIBRARY)

bool DISCORD_SCRIPT_LIBRARY::Init()
{
    constexpr ScriptFunction functions[] = {{"DiscordInitialize", 3, answer<0, 3>},
                                            {"DiscordSetRichPresence", 2, answer<0, 2>}};
    for (const auto &function : functions)
    {
        IFUNCINFO info;
        info.nArguments = function.arguments;
        info.pFuncName = function.name;
        info.pReturnValueName = "int";
        info.pFuncAddress = function.function;
        core.SetScriptFunction(&info);
    }
    // the current Unix time in seconds, as text
    IFUNCINFO info;
    info.nArguments = 0;
    info.pFuncName = "DiscordGetSystemTime";
    info.pReturnValueName = "string";
    info.pFuncAddress = [](VS_STACK *stack) -> uint32_t {
        auto *value = stack->Push();
        if (!value)
            return IFUNCRESULT_FAILED;
        value->Set(std::to_string(static_cast<long long>(std::time(nullptr))).c_str());
        return IFUNCRESULT_OK;
    };
    core.SetScriptFunction(&info);
    return true;
}

bool SteamApiScriptLib::Init()
{
    ReadPlayerSettings();
    for (const auto &function : kFunctions)
    {
        IFUNCINFO sIFuncInfo;
        sIFuncInfo.nArguments = function.arguments;
        sIFuncInfo.pFuncName = function.name;
        sIFuncInfo.pReturnValueName = "int";
        sIFuncInfo.pFuncAddress = function.function;
        core.SetScriptFunction(&sIFuncInfo);
    }
    IFUNCINFO info;
    info.nArguments = 0;
    info.pFuncName = "GetPersonaName";
    info.pReturnValueName = "string";
    info.pFuncAddress = GetPersonaName;
    core.SetScriptFunction(&info);
    info.pFuncName = "Verify";
    info.pReturnValueName = "void";
    info.pFuncAddress = Verify;
    core.SetScriptFunction(&info);
    return true;
}
} // namespace steamapi
