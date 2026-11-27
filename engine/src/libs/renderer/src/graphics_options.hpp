#pragma once

// The desktop's graphics options: one table (graphics_options.cpp) that the scripts' natives, engine.ini and the
// documentation read. README.md, "Graphics options".

#include "storm/render/effects.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace storm::render
{
enum class OptionKind : uint8_t
{
    Toggle, // 0 off, 1 on
    Choice, // one of `choices`
    Number, // any whole number of min..max
};

enum class OptionTakes : uint8_t
{
    Live,      // drawn at once
    NextStart, // kept, taken at the next start
};

struct GraphicsOption
{
    const char *name; // the scripts' name and the engine.ini key
    OptionKind kind;
    int min, max;
    std::span<const int> choices; // Toggle and Choice: the values, in their order on a screen
    int desktop;                  // the desktop's default: the full quality
    OptionTakes takes;
    bool generated_maps; // only for a game whose texture maps the engine makes (GameProfileFacts)
    const char *values;  // what the values mean
    // The effects options a value stands for; nullptr: the renderer reads the option itself.
    void (*effects)(int value, uint32_t display_height, std::vector<EffectsOption> &out);
};

std::span<const GraphicsOption> GraphicsOptions();
const GraphicsOption *FindGraphicsOption(std::string_view name);
const char *GraphicsOptionKindName(OptionKind kind); // "toggle", "choice", "number"
bool GraphicsOptionAllows(const GraphicsOption &option, int value);
// The effects options of `value` on a display `display_height` pixels tall.
std::vector<EffectsOption> GraphicsOptionEffects(const GraphicsOption &option, int value, uint32_t display_height);
// The table for README.md, "Graphics options" (engine --graphics-options prints it).
std::string GraphicsOptionsMarkdown();

// ---- the values of the running game's options (graphics_option_values.cpp) ----

// What the renderer adds: the options applied, and the value of an option that runs (the device or another option
// may keep a value from running). Without a renderer (script checks) nothing is applied and the kept value is
// what runs.
struct GraphicsOptionsRenderer
{
    std::function<void(const GraphicsOption &)> changed;
    std::function<int(const GraphicsOption &, int kept)> running;
};
void SetGraphicsOptionsRenderer(GraphicsOptionsRenderer renderer);
// Whether this game has the option (GraphicsOption::generated_maps).
bool GraphicsOptionOffered(const GraphicsOption &option);
// The kept value: engine.ini of the game's settings folder on desktop, read once; else the desktop default.
int KeptGraphicsOption(const GraphicsOption &option);
// The value that runs (GraphicsOptionsRenderer::running).
int RunningGraphicsOption(const GraphicsOption &option);
// Keeps an allowed value (desktop: written to engine.ini) and tells the renderer. False, logged: not allowed.
bool SetGraphicsOption(const GraphicsOption &option, int value);
} // namespace storm::render
