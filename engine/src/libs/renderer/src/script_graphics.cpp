// The Graphics_* natives (README.md, "Graphics options of the game's scripts").

#include "script_graphics.hpp"

#include "core.h"
#include "renderer_impl.h"
#include "s_import_func.h"
#include "storm/render/perf_stats.hpp"
#include "v_file_service.h"
#include "v_s_stack.h"

#include <SDL_cpuinfo.h>
#include <spdlog/spdlog.h>

#include <utility>

#include <set>
#include <string>

namespace storm::render
{
namespace
{
ScriptGraphics graphics;
// The colour ramp of the window (Renderer::SetColorParameters), which the legacy games' own sliders also set.
float gamma = 1.0f, contrast = 1.0f, brightness = 0.0f;

int32_t IntArg(VS_STACK *stack)
{
    int32_t value = 0;
    static_cast<VDATA *>(stack->Pop())->Get(value);
    return value;
}

float FloatArg(VS_STACK *stack)
{
    float value = 0.0f;
    static_cast<VDATA *>(stack->Pop())->Get(value);
    return value;
}

uint32_t Return(VS_STACK *stack, int32_t value)
{
    static_cast<VDATA *>(stack->Push())->Set(value);
    return IFUNCRESULT_OK;
}

uint32_t Return(VS_STACK *stack, float value)
{
    static_cast<VDATA *>(stack->Push())->Set(value);
    return IFUNCRESULT_OK;
}

// What this renderer does not have: said once per native, when the scripts ask for something else than its answer.
void NotAvailable(const char *native, const char *what)
{
    static std::set<std::string> said;
    if (said.insert(native).second)
        spdlog::warn("{}: {}", native, what);
}

// The window's size, kept by the engine across starts: engine.ini of the player's settings folder, as the game's own
// engine keeps it.
std::unique_ptr<INIFILE> EngineIni()
{
    auto ini = fio->OpenIniFile(core.EngineIniFileName());
    if (!ini)
        spdlog::error("graphics options: cannot open {}", core.EngineIniFileName());
    return ini;
}

int32_t IniValue(const char *key, int32_t fallback)
{
    const auto ini = EngineIni();
    return ini ? ini->GetInt(nullptr, key, fallback) : fallback;
}

void KeepInIni(const char *key, int32_t value)
{
    if (const auto ini = EngineIni())
    {
        ini->WriteLong(nullptr, key, value);
        spdlog::info("graphics options: {} = {} kept for the next start", key, value);
    }
}

int Running(const char *option)
{
    return RunningGraphicsOption(*FindGraphicsOption(option));
}

void SetOption(const char *option, int value)
{
    SetGraphicsOption(*FindGraphicsOption(option), value);
}

// The scripts' choices as the engine's options (kept and applied; on Android the app's graphics settings decide).
uint32_t Apply(VS_STACK *)
{
#ifdef __ANDROID__
    static bool said = false;
    if (!std::exchange(said, true))
        spdlog::info("graphics options of the game's scripts: the app's graphics settings decide on Android");
#endif
    for (const auto &[option, value] : ScriptGraphicsOptions(graphics))
        SetOption(option, value);
    return IFUNCRESULT_OK;
}

void ApplyColours()
{
    // The game's brightness is an exposure offset (1 + brightness, at least 0.1): a gain on the ramp.
    Renderer::pRS->SetColorParameters(gamma, 0.0f, contrast * std::max(0.1f, 1.0f + brightness));
}

uint32_t SetShadows(VS_STACK *stack)
{
    graphics.shadows = IntArg(stack);
    return Apply(stack);
}

uint32_t SetDynamicShadows(VS_STACK *stack)
{
    graphics.lamp_shadows = std::clamp(IntArg(stack), 0, 3);
    if (*graphics.lamp_shadows > 0)
        NotAvailable("Graphics_SetDynamicShadows", "the lamps' shadow size is the preset's; only off is taken");
    return Apply(stack);
}

uint32_t GetDynamicShadows(VS_STACK *stack)
{
    return Return(stack, Running("lamp_shadows") == 0 ? 0 : graphics.lamp_shadows.value_or(3));
}

// Characters cast the sun's shadows with everything else: they have them whenever shadows are on.
uint32_t SetCharacterShadows(VS_STACK *stack)
{
    if ((IntArg(stack) != 0) != (Running("sun_shadows") != 0))
        NotAvailable("Graphics_SetCharacterShadows", "characters cast shadows exactly when shadows are on");
    return IFUNCRESULT_OK;
}

uint32_t GetCharacterShadows(VS_STACK *stack)
{
    return Return(stack, int32_t{Running("sun_shadows") != 0});
}

uint32_t SetSSAO(VS_STACK *stack)
{
    graphics.ambient_occlusion = IntArg(stack);
    return Apply(stack);
}

uint32_t SetAOMode(VS_STACK *stack)
{
    if (IntArg(stack) != 0)
        NotAvailable("Graphics_SetAOMode", "one occlusion method only (mode 0)");
    return IFUNCRESULT_OK;
}

uint32_t GetZero(VS_STACK *stack)
{
    return Return(stack, int32_t{0});
}

uint32_t SetSeaReflections(VS_STACK *stack)
{
    if (IntArg(stack) == 0)
        NotAvailable("Graphics_SetSeaReflections", "the sea always reflects");
    return IFUNCRESULT_OK;
}

uint32_t GetOne(VS_STACK *stack)
{
    return Return(stack, int32_t{1});
}

// The game's quality 1 of its FFT ocean (textures x1, geometry x2, refraction), which our sea stands for. Its sea
// detail (grid, distance) is the scripts' own Sea attributes.
constexpr float kSeaTextures = 1.0f;
constexpr int32_t kSeaGeometry = 2;

uint32_t GetSeaDetail(VS_STACK *stack)
{
    auto *reference = (VDATA *)static_cast<VDATA *>(stack->Pop())->GetReference();
    ATTRIBUTES *target = reference ? reference->GetAClass() : nullptr;
    if (!target)
    {
        spdlog::error("Graphics_GetSeaDetail: the argument is not a reference to an object");
        return IFUNCRESULT_FAILED;
    }
    target->SetAttributeUseDword("SeaQuality", 1);
    target->SetAttributeUseFloat("SeaTextureMultiplier", kSeaTextures);
    target->SetAttributeUseDword("SeaGeometryMultiplier", kSeaGeometry);
    target->SetAttributeUseDword("SeaRefraction", 1);
    return IFUNCRESULT_OK;
}

// Another quality is refused (0): the options screen then shows the one GetSeaDetail answers.
uint32_t SetSeaDetail(VS_STACK *stack)
{
    const bool refraction = IntArg(stack) != 0;
    const int32_t geometry = IntArg(stack);
    const float textures = FloatArg(stack);
    const bool ours = refraction && geometry == kSeaGeometry && textures == kSeaTextures;
    if (!ours)
        NotAvailable("Graphics_SetSeaDetail", "one sea quality only (textures x1, geometry x2, refraction)");
    return Return(stack, int32_t{ours});
}

// The game's TAA and DLAA, asked by Graphics_SetTAA and Graphics_SetDLSS (ScriptAntiAliasingOf draws them).
bool taaAsked = false, dlssAsked = false;

// Its TAA or DLAA is drawn as its supersampling «High» (MSAA 4x, ScriptAntiAliasingOf), and the options it keeps
// (InterfaceStates) say so: none of its getters claims a TAA or DLSS that does not run, so its options screen falls
// back to the supersampling it keeps and shows that.
uint32_t ApplyAntiAliasing(VS_STACK *stack)
{
    graphics.temporal = taaAsked || dlssAsked;
    if (graphics.temporal)
    {
        static bool said = false;
        if (!std::exchange(said, true))
            spdlog::info("graphics options: the game's TAA or DLAA is drawn as MSAA 4x with alpha to coverage, shown "
                         "as its supersampling High");
        auto *states = static_cast<VDATA *>(core.GetScriptVariable("InterfaceStates"));
        if (ATTRIBUTES *options = states ? states->GetAClass() : nullptr)
        {
            options->SetAttribute("SSAA", "1");
            options->SetAttribute("FXAA", "-1");
        }
        else
            spdlog::error("graphics options: no InterfaceStates to show the anti-aliasing in");
    }
    return Apply(stack);
}

// As the game's own engine: TAA on turns its FXAA and supersampling off, any call turns DLSS off.
uint32_t SetTAA(VS_STACK *stack)
{
    taaAsked = IntArg(stack) != 0;
    dlssAsked = false;
    if (taaAsked)
        graphics.fxaa = graphics.ssaa = 0;
    return ApplyAntiAliasing(stack);
}

float taaSharpness = 0.5f;

uint32_t SetTAASharpness(VS_STACK *stack)
{
    taaSharpness = std::clamp(FloatArg(stack), 0.0f, 1.0f);
    NotAvailable("Graphics_SetTAASharpness", "no temporal anti-aliasing; the value is only shown");
    return IFUNCRESULT_OK;
}

uint32_t GetTAASharpness(VS_STACK *stack)
{
    return Return(stack, taaSharpness);
}

// Its DLSS modes (0 DLAA, 1..4 upscaling) are all anti-aliasing at heart: drawn as its TAA is.
uint32_t SetDLSS(VS_STACK *stack)
{
    dlssAsked = IntArg(stack) >= 0;
    if (dlssAsked)
    {
        NotAvailable("Graphics_SetDLSS", "no DLSS (NVIDIA only): drawn as MSAA 4x");
        graphics.fxaa = graphics.ssaa = 0;
    }
    return ApplyAntiAliasing(stack);
}

uint32_t GetDLSS(VS_STACK *stack)
{
    return Return(stack, int32_t{-1});
}

uint32_t SetFrameGeneration(VS_STACK *stack)
{
    if (IntArg(stack) != 0)
        NotAvailable("Graphics_SetFrameGeneration", "no frame generation (NVIDIA only)");
    return IFUNCRESULT_OK;
}

// Its FSR modes (0..4) are upscaling and anti-aliasing of AMD's library, which this renderer does not have; -1 is off.
uint32_t SetFSR(VS_STACK *stack)
{
    if (IntArg(stack) >= 0)
        NotAvailable("Graphics_SetFSR", "no FSR upscaling");
    return IFUNCRESULT_OK;
}

uint32_t GetFSR(VS_STACK *stack)
{
    return Return(stack, int32_t{-1});
}

// The game's island foliage (Resource/INI/foliage.json) is not drawn by this renderer: the density is kept for the
// options screen only.
float islandFoliageDensity = 1.0f;

uint32_t SetIslandFoliageDensity(VS_STACK *stack)
{
    islandFoliageDensity = FloatArg(stack);
    NotAvailable("Graphics_SetIslandFoliageDensity", "island foliage (foliage.json) is not drawn");
    return IFUNCRESULT_OK;
}

uint32_t GetIslandFoliageDensity(VS_STACK *stack)
{
    return Return(stack, islandFoliageDensity);
}

uint32_t SetVSync(VS_STACK *stack)
{
    const bool on = IntArg(stack) != 0;
#ifdef __ANDROID__
    if (!on)
        NotAvailable("Graphics_SetVSync", "a phone always presents in step with the display");
#else
    SetOption("vsync", on);
#endif
    return IFUNCRESULT_OK;
}

uint32_t GetVSync(VS_STACK *stack)
{
    return Return(stack, int32_t{Running("vsync")});
}

uint32_t SetFXAA(VS_STACK *stack)
{
    graphics.fxaa = IntArg(stack);
    if (*graphics.fxaa > 0)
    {
        graphics.ssaa = 0;
        taaAsked = dlssAsked = false;
    }
    return ApplyAntiAliasing(stack);
}

uint32_t SetSSAA(VS_STACK *stack)
{
    graphics.ssaa = IntArg(stack);
    if (*graphics.ssaa > 0)
    {
        graphics.fxaa = 0;
        taaAsked = dlssAsked = false;
        NotAvailable("Graphics_SetSSAA", "supersampling is drawn as MSAA");
    }
    return ApplyAntiAliasing(stack);
}

// The window's size and mode are taken at the start (engine.ini screen_x, screen_y, full_screen), as in the game's
// own engine.
uint32_t SetDisplayResolution(VS_STACK *stack)
{
    const int32_t height = IntArg(stack);
    const int32_t width = IntArg(stack);
#ifdef __ANDROID__
    NotAvailable("Graphics_SetDisplayResolution", "the picture fills the phone's screen");
#else
    KeepInIni("screen_x", width);
    KeepInIni("screen_y", height);
#endif
    return IFUNCRESULT_OK;
}

uint32_t GetDisplayResolutions(VS_STACK *stack)
{
    const char *name = nullptr;
    static_cast<VDATA *>(stack->Pop())->Get(name);
    auto *variable = name ? static_cast<VDATA *>(core.GetScriptVariable(name)) : nullptr;
    ATTRIBUTES *list = variable ? variable->GetAClass() : nullptr;
    if (!list)
    {
        spdlog::error("Graphics_GetDisplayResolutions: no script object '{}'", name ? name : "");
        return IFUNCRESULT_FAILED;
    }
    while (list->GetAttributesNum() > 0)
        list->DeleteAttributeClassX(list->GetAttributeClass(0u));
    const auto modes = core.GetWindow()->DisplayModes();
    if (modes.empty())
        spdlog::error("Graphics_GetDisplayResolutions: the display's modes are unknown");
    const std::pair current{IniValue("screen_x", 1024), IniValue("screen_y", 768)};
    for (const auto &[width, height] : ScriptDisplaySizes(modes))
    {
        ATTRIBUTES &size = list->CreateAttribute(std::to_string(width) + "x" + std::to_string(height));
        size.SetAttribute("width", std::to_string(width));
        size.SetAttribute("height", std::to_string(height));
        if (std::pair{width, height} == current)
            size.SetAttribute("enabled", "1");
    }
    return IFUNCRESULT_OK;
}

uint32_t SetFullScreen(VS_STACK *stack)
{
    const int32_t state = IntArg(stack);
#ifdef __ANDROID__
    NotAvailable("Graphics_SetFullScreen", "the picture fills the phone's screen");
#else
    SetOption("full_screen", state != 0);
#endif
    return IFUNCRESULT_OK;
}

uint32_t GetFullScreen(VS_STACK *stack)
{
    return Return(stack, KeptGraphicsOption(*FindGraphicsOption("full_screen")));
}

uint32_t SetAnisotropicFiltering(VS_STACK *stack)
{
    graphics.anisotropy = IntArg(stack);
    return Apply(stack);
}

uint32_t PreviewAnisotropicFiltering(VS_STACK *stack)
{
    graphics.anisotropy = IntArg(stack);
    return Apply(stack);
}

uint32_t GetAnisotropicFiltering(VS_STACK *stack)
{
    // The game's off is 0, ours level 1.
    const int level = Running("anisotropic_filtering");
    return Return(stack, level <= 1 ? 0 : level);
}

// MiB, as the game's own engine answers (the options screen warns when its settings need more). Where the backend
// cannot tell (bgfx's OpenGL), the GPU is taken to share the system's memory, of which graphics drivers give it about
// half (Windows' shared GPU memory, Mesa's default GTT size): half the RAM, logged once.
uint32_t GetVRAMBudget(VS_STACK *stack)
{
    double mib = static_cast<double>(GpuMemoryBudget()) / (1024.0 * 1024.0);
    if (mib == 0.0)
    {
        mib = SDL_GetSystemRAM() / 2.0;
        static bool said = false;
        if (!std::exchange(said, true))
            spdlog::info("Graphics_GetVRAMBudget: the GPU does not tell its memory; estimated {:.0f} MiB, half the "
                         "system's {} MiB",
                         mib, SDL_GetSystemRAM());
    }
    return Return(stack, static_cast<float>(mib));
}

uint32_t SetGamma(VS_STACK *stack)
{
    gamma = FloatArg(stack);
    ApplyColours();
    return IFUNCRESULT_OK;
}

uint32_t SetContrast(VS_STACK *stack)
{
    contrast = FloatArg(stack);
    ApplyColours();
    return IFUNCRESULT_OK;
}

uint32_t SetBrightness(VS_STACK *stack)
{
    brightness = FloatArg(stack);
    ApplyColours();
    return IFUNCRESULT_OK;
}

uint32_t SetTemperature(VS_STACK *stack)
{
    graphics.temperature = FloatArg(stack);
    return Apply(stack);
}

uint32_t GetTemperature(VS_STACK *stack)
{
    return Return(stack, static_cast<float>(Running("colour_temperature") - 100) / 100.0f);
}

uint32_t SetSaturation(VS_STACK *stack)
{
    graphics.saturation = FloatArg(stack);
    return Apply(stack);
}

uint32_t GetSaturation(VS_STACK *stack)
{
    return Return(stack, static_cast<float>(Running("colour_saturation")) / 100.0f);
}

struct Native
{
    const char *name;
    int32_t arguments;
    const char *returns; // nullptr: void
    uint32_t (*function)(VS_STACK *);
};

constexpr Native kNatives[] = {
    {"Graphics_SetShadows", 1, nullptr, SetShadows},
    {"Graphics_SetDynamicShadows", 1, nullptr, SetDynamicShadows},
    {"Graphics_GetDynamicShadows", 0, "int", GetDynamicShadows},
    {"Graphics_SetCharacterShadows", 1, nullptr, SetCharacterShadows},
    {"Graphics_GetCharacterShadows", 0, "int", GetCharacterShadows},
    {"Graphics_SetSSAO", 1, nullptr, SetSSAO},
    {"Graphics_SetAOMode", 1, nullptr, SetAOMode},
    {"Graphics_GetAOMode", 0, "int", GetZero},
    {"Graphics_SetSeaReflections", 1, nullptr, SetSeaReflections},
    {"Graphics_GetSeaReflections", 0, "int", GetOne},
    {"Graphics_GetSeaDetail", 1, nullptr, GetSeaDetail},
    {"Graphics_SetSeaDetail", 3, "int", SetSeaDetail},
    {"Graphics_SetTAA", 1, nullptr, SetTAA},
    {"Graphics_GetTAA", 0, "int", GetZero},
    {"Graphics_SetTAASharpness", 1, nullptr, SetTAASharpness},
    {"Graphics_GetTAASharpness", 0, "float", GetTAASharpness},
    {"Graphics_IsDLAASupported", 0, "int", GetZero},
    {"Graphics_IsDLSSSupported", 0, "int", GetZero},
    {"Graphics_SetDLSS", 1, nullptr, SetDLSS},
    {"Graphics_GetDLSS", 0, "int", GetDLSS},
    {"Graphics_IsFrameGenerationSupported", 0, "int", GetZero},
    {"Graphics_SetFrameGeneration", 1, nullptr, SetFrameGeneration},
    {"Graphics_GetFrameGeneration", 0, "int", GetZero},
    {"Graphics_IsFSRSupported", 0, "int", GetZero},
    {"Graphics_SetFSR", 1, nullptr, SetFSR},
    {"Graphics_GetFSR", 0, "int", GetFSR},
    {"Graphics_IsFSRFrameGenerationSupported", 0, "int", GetZero},
    {"Graphics_SetIslandFoliageDensity", 1, nullptr, SetIslandFoliageDensity},
    {"Graphics_GetIslandFoliageDensity", 0, "float", GetIslandFoliageDensity},
    {"Graphics_SetVSync", 1, nullptr, SetVSync},
    {"Graphics_GetVSync", 0, "int", GetVSync},
    {"Graphics_SetFXAA", 1, nullptr, SetFXAA},
    {"Graphics_SetSSAA", 1, nullptr, SetSSAA},
    {"Graphics_SetDisplayResolution", 2, nullptr, SetDisplayResolution},
    {"Graphics_GetDisplayResolutions", 1, nullptr, GetDisplayResolutions},
    {"Graphics_SetFullScreen", 1, nullptr, SetFullScreen},
    {"Graphics_GetFullScreen", 0, "int", GetFullScreen},
    {"Graphics_SetAnisotropicFiltering", 1, nullptr, SetAnisotropicFiltering},
    {"Graphics_PreviewAnisotropicFiltering", 1, nullptr, PreviewAnisotropicFiltering},
    {"Graphics_GetAnisotropicFiltering", 0, "int", GetAnisotropicFiltering},
    {"Graphics_GetVRAMBudget", 0, "float", GetVRAMBudget},
    {"Graphics_SetGamma", 1, nullptr, SetGamma},
    {"Graphics_SetContrast", 1, nullptr, SetContrast},
    {"Graphics_SetBrightness", 1, nullptr, SetBrightness},
    {"Graphics_SetTemperature", 1, nullptr, SetTemperature},
    {"Graphics_GetTemperature", 0, "float", GetTemperature},
    {"Graphics_SetSaturation", 1, nullptr, SetSaturation},
    {"Graphics_GetSaturation", 0, "float", GetSaturation},
};
} // namespace

void RegisterScriptGraphics()
{
    for (const Native &native : kNatives)
    {
        IFUNCINFO info;
        info.pFuncName = native.name;
        info.nArguments = native.arguments;
        info.pReturnValueName = native.returns;
        info.pFuncAddress = native.function;
        core.SetScriptFunction(&info);
    }
}
} // namespace storm::render
