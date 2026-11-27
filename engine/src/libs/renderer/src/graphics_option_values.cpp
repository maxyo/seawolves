// The running game's graphics options and their natives (README.md, "Graphics options").

#include "graphics_options.hpp"

#include "core.h"
#include "game_profile.hpp"
#include "s_import_func.h"
#include "script_libriary.h"
#include "v_s_stack.h"
#include "vma.hpp"

#include <spdlog/spdlog.h>

#include <map>
#include <optional>
#include <string>

namespace storm::render
{
namespace
{
GraphicsOptionsRenderer renderer;

// Desktop: engine.ini of the game's settings folder holds the kept values under the options' names. Android: the
// app's graphics settings decide and the values only answer the scripts (README.md).
std::map<std::string, int> &Kept()
{
    static std::map<std::string, int> kept = [] {
        std::map<std::string, int> values;
#ifndef __ANDROID__
        const auto ini = fio->OpenIniFile(core.EngineIniFileName());
        if (!ini)
            spdlog::error("graphics options: cannot open {}; the desktop defaults", core.EngineIniFileName());
        for (const GraphicsOption &option : GraphicsOptions())
        {
            const int value = ini ? ini->GetInt(nullptr, option.name, option.desktop) : option.desktop;
            if (GraphicsOptionAllows(option, value))
                values[option.name] = value;
            else
                spdlog::error("graphics options: engine.ini {} = {} is not one of its values ({}); the default {}",
                              option.name, value, option.values, option.desktop);
        }
#endif
        return values;
    }();
    return kept;
}

void Keep(const GraphicsOption &option, int value)
{
    Kept()[option.name] = value;
#ifndef __ANDROID__
    if (const auto ini = fio->OpenIniFile(core.EngineIniFileName()))
        ini->WriteLong(nullptr, option.name, value);
    else
        spdlog::error("graphics options: cannot open {}; {} = {} is not kept", core.EngineIniFileName(), option.name,
                      value);
#endif
}

const GraphicsOption *OptionArg(VS_STACK *stack, const char *native)
{
    const char *name = nullptr;
    static_cast<VDATA *>(stack->Pop())->Get(name);
    const GraphicsOption *option = name ? FindGraphicsOption(name) : nullptr;
    if (!option || !GraphicsOptionOffered(*option))
        spdlog::error("{}: no graphics option '{}' for this game", native, name ? name : "");
    return option && GraphicsOptionOffered(*option) ? option : nullptr;
}

// -1 for an option the game does not have (logged as an error).
uint32_t GetOption(VS_STACK *stack)
{
    const GraphicsOption *option = OptionArg(stack, "Graphics_GetOption");
    static_cast<VDATA *>(stack->Push())->Set(static_cast<int32_t>(option ? RunningGraphicsOption(*option) : -1));
    return IFUNCRESULT_OK;
}

// 1 taken, 0 refused: a value the option does not have or an option the game does not have (logged as errors).
uint32_t SetOption(VS_STACK *stack)
{
    int32_t value = 0;
    static_cast<VDATA *>(stack->Pop())->Get(value);
    const GraphicsOption *option = OptionArg(stack, "Graphics_SetOption");
    static_cast<VDATA *>(stack->Push())->Set(int32_t{option && SetGraphicsOption(*option, value)});
    return IFUNCRESULT_OK;
}

// <object>.<option>: kind, min, max, default, value (what runs), kept, start (1: taken at the next start) and for a
// toggle or a choice its values in order, choices.v0, v1, ... The game's options in the table's order.
uint32_t ListOptions(VS_STACK *stack)
{
    auto *reference = (VDATA *)static_cast<VDATA *>(stack->Pop())->GetReference();
    ATTRIBUTES *list = reference ? reference->GetAClass() : nullptr;
    if (!list)
    {
        spdlog::error("Graphics_ListOptions: the argument is not a reference to an object");
        return IFUNCRESULT_FAILED;
    }
    while (list->GetAttributesNum() > 0)
        list->DeleteAttributeClassX(list->GetAttributeClass(0u));
    for (const GraphicsOption &option : GraphicsOptions())
    {
        if (!GraphicsOptionOffered(option))
            continue;
        ATTRIBUTES &row = list->CreateAttribute(option.name);
        row.SetAttribute("kind", std::string_view(GraphicsOptionKindName(option.kind)));
        row.SetAttributeUseDword("min", static_cast<uint32_t>(option.min));
        row.SetAttributeUseDword("max", static_cast<uint32_t>(option.max));
        row.SetAttributeUseDword("default", static_cast<uint32_t>(option.desktop));
        row.SetAttributeUseDword("value", static_cast<uint32_t>(RunningGraphicsOption(option)));
        row.SetAttributeUseDword("kept", static_cast<uint32_t>(KeptGraphicsOption(option)));
        row.SetAttributeUseDword("start", option.takes == OptionTakes::NextStart);
        if (!option.choices.empty())
        {
            ATTRIBUTES &choices = row.CreateAttribute("choices");
            for (size_t i = 0; i < option.choices.size(); ++i)
                choices.SetAttributeUseDword(("v" + std::to_string(i)).c_str(),
                                             static_cast<uint32_t>(option.choices[i]));
        }
    }
    return IFUNCRESULT_OK;
}

class GraphicsOptionsScriptLib : public SCRIPT_LIBRIARY
{
  public:
    bool Init() override
    {
#ifndef __ANDROID__
        struct Native
        {
            const char *name;
            int32_t arguments;
            const char *returns;
            uint32_t (*function)(VS_STACK *);
        };
        for (const Native &native :
             {Native{"Graphics_GetOption", 1, "int", GetOption}, Native{"Graphics_SetOption", 2, "int", SetOption},
              Native{"Graphics_ListOptions", 1, nullptr, ListOptions}})
        {
            IFUNCINFO info;
            info.pFuncName = native.name;
            info.nArguments = native.arguments;
            info.pReturnValueName = native.returns;
            info.pFuncAddress = native.function;
            core.SetScriptFunction(&info);
        }
#endif
        return true;
    }
};
} // namespace

CREATE_SCRIPTLIBRIARY(GraphicsOptionsScriptLib)

void SetGraphicsOptionsRenderer(GraphicsOptionsRenderer next)
{
    renderer = std::move(next);
}

bool GraphicsOptionOffered(const GraphicsOption &option)
{
    return !option.generated_maps || gameProfileFacts().generates_texture_maps;
}

int KeptGraphicsOption(const GraphicsOption &option)
{
    const auto &kept = Kept();
    const auto found = kept.find(option.name);
    return found == kept.end() ? option.desktop : found->second;
}

int RunningGraphicsOption(const GraphicsOption &option)
{
    const int kept = KeptGraphicsOption(option);
    return renderer.running ? renderer.running(option, kept) : kept;
}

bool SetGraphicsOption(const GraphicsOption &option, int value)
{
    if (!GraphicsOptionAllows(option, value))
    {
        spdlog::error("graphics options: {} = {} refused: {}", option.name, value, option.values);
        return false;
    }
    if (KeptGraphicsOption(option) == value)
        return true;
    Keep(option, value);
    spdlog::info("graphics options: {} = {}{}", option.name, value,
                 option.takes == OptionTakes::NextStart ? ", taken at the next start" : "");
    if (renderer.changed)
        renderer.changed(option);
    return true;
}
} // namespace storm::render
