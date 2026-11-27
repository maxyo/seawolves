#include "steam_api_script_lib.hpp"

#include "core.h"
#include "s_import_func.h"
#include "v_s_stack.h"
#include "vma.hpp"

#include <ctime>
#include <string>

// The game scripts call the Steam functions of the original PC release (achievements, stats, DLC
// checks). There is no Steam on our platforms: the functions keep their script signatures and
// report "no Steam", every DLC owned (the DLC content ships with the game), no achievements/stats.
namespace steamapi
{
CREATE_SCRIPTLIBRIARY(SteamApiScriptLib)

namespace
{
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
    {"GetSteamEnabled", 0, answer<0, 0>},  {"GetDLCenabled", 1, answer<1, 1>},
    {"GetDLCCount", 0, answer<0, 0>},      {"GetDLCData", 1, answer<0, 1>},
    {"DLCStartOverlay", 1, answer<0, 1>},  {"SetAchievement", 1, answer<0, 1>},
    {"GetAchievement", 1, answer<0, 1>},   {"SetStat", 2, answer<0, 2>},
    {"GetStat", 1, answer<0, 1>},          {"StoreStats", 0, answer<0, 0>},
    {"ClearAchievement", 1, answer<0, 1>}, {"ResetStats", 1, answer<0, 1>},
    // Caribbean Legend's Steam account, overlay, rich presence, Workshop, DLC and curator queries: no Steam client,
    // no overlay, no Workshop items; a DLC whose content is not in the game is not owned; curator status -1 is the
    // scripts' "not available". The ownership check (IsVerified, Verify) is not provided.
    {"GetSteamID", 0, answer<0, 0>},       {"GetAppID", 0, answer<0, 0>},
    {"IsSteamDeck", 0, answer<0, 0>},      {"IsVirtualKeyboardShowing", 0, answer<0, 0>},
    {"ShowSteamVirtualKeyboard", 5, answer<0, 5>}, {"GetOverlaysInfo", 1, answer<0, 1>},
    {"GameOverlayToWebPage", 1, answer<0, 1>}, {"GameOverlayToWebPageModal", 1, answer<0, 1>},
    {"SteamSetRichPresence", 2, answer<0, 2>}, {"SteamClearRichPresence", 0, answer<0, 0>},
    {"BIsSubscribedApp", 1, answer<0, 1>}, {"BeginCuratorCheckAsync", 2, answer<-1, 2>},
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
    for (const auto &function : kFunctions)
    {
        IFUNCINFO sIFuncInfo;
        sIFuncInfo.nArguments = function.arguments;
        sIFuncInfo.pFuncName = function.name;
        sIFuncInfo.pReturnValueName = "int";
        sIFuncInfo.pFuncAddress = function.function;
        core.SetScriptFunction(&sIFuncInfo);
    }
    return true;
}
} // namespace steamapi
