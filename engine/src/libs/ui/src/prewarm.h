// The fonts the first opening of a screen would make, made ahead (Host::Prewarm).
//
// Without it a screen's first frame pays for its fonts: RmlUi makes a font face handle for every family, style, weight
// and pixel size the first time an element needs it, loads its glyphs (rmlui-glyph-blocks.patch) and draws its layer
// textures, the font effects on the CPU. (Icons cost nothing at first draw: they come from the pack's baked atlas.)
//
// The pack lists what its screens and HUD draw (pack.json "prewarm": {layoutWidth, fonts: [FontUse]}, written and
// checked by storm_ui_pack_check over every recorded model and the states its taps reach). Warmer makes them one step
// at a time for the game language: each font use's handle at the layout's dp ratio with the glyph blocks of the
// language's UI strings, its font effects and layer textures, so a later text of that use costs what it costs on a
// second opening.
#pragma once

#include <nlohmann/json.hpp>

#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace Rml
{
class Element;
class RenderManager;
} // namespace Rml

namespace storm::ui
{

using Json = nlohmann::json;

// One font configuration a text of the pack is drawn with. `size` is in dp (RmlUi's handle is the computed pixel
// size, truncated: the layout's dp ratio times this); `effect` is the font-effect declaration ("" for none).
// With `min` (dp) it is the step-down of a <ui-clamp> instead: a text that does not fit at `size` is measured, not
// drawn, at sizes down to the one that fits, `min` (draw_elements.cpp Fit), each a handle with its glyphs; `min` equal
// to `size` is a clamp whose text fitted. In a layout narrower than the list's, the range reaches further down.
// With `handle` it is the font of an element without text of its own: RmlUi makes the handle for every element's
// font (ElementStyle), with ASCII and the Latin-1 supplement; no glyph block, effect or texture of it is needed.
struct FontUse
{
    std::string family;
    bool italic = false;
    int weight = 400;
    double size = 0.0;
    std::string effect;
    double min = 0.0;
    bool handle = false;

    bool operator<(const FontUse &other) const
    {
        return std::tie(family, italic, weight, size, effect, min, handle) <
               std::tie(other.family, other.italic, other.weight, other.size, other.effect, other.min, other.handle);
    }
    // The same font face handle (family, style, weight, size).
    bool SameHandle(const FontUse &other) const
    {
        return std::tie(family, italic, weight, size) == std::tie(other.family, other.italic, other.weight, other.size);
    }
    bool operator==(const FontUse &other) const
    {
        return !(*this < other) && !(other < *this);
    }
};

Json FontUseJson(const FontUse &use);
// False (and `why`) for an entry that is not a font use.
bool ParseFontUse(const Json &entry, FontUse &use, std::string &why);
// The font uses under `root`: of its texts, of its <ui-clamp> step-downs and of its other elements' fonts, sizes in dp
// of `dp_ratio`.
void CollectFontUses(Rml::Element *root, float dp_ratio, std::set<FontUse> &out);
// `uses` without the handle-only uses whose handle another use makes too (the pack's list).
std::set<FontUse> WithoutCoveredHandles(const std::set<FontUse> &uses);
// Whether `listed` makes what `use` needs: the use itself, or for a handle-only use any use of its handle.
bool Covers(const std::set<FontUse> &listed, const FontUse &use);

// One character of every 128-code-point block the text holds (ASCII's first): the glyph blocks a font face handle
// needs for it (rmlui-glyph-blocks.patch loads a block with any of its characters).
std::string BlockSample(const std::string &utf8);

class Warmer
{
  public:
    // A new list, a new language or a new dp ratio starts over (handles made already cost nothing again).
    // `narrower`: the layout's width over the width the list was recorded at, at most 1: a step-down fits its text in
    // a narrower layout at a smaller size, so the measured sizes reach that much further down.
    void Reset(std::vector<FontUse> uses, std::string sample, float dp_ratio, double narrower);
    // Makes the next font use: handle, glyph blocks, effects, layer textures (through `render`); a step-down's handles
    // with their glyph blocks. False when everything is made. `made_ms` is what it took.
    bool Step(Rml::RenderManager &render, double &made_ms, std::string &error);
    bool Done() const
    {
        return next_ >= uses_.size();
    }
    size_t Count() const
    {
        return uses_.size();
    }
    size_t Made() const
    {
        return next_;
    }

  private:
    std::vector<FontUse> uses_;
    std::string sample_;
    float dp_ratio_ = 1.0f;
    double narrower_ = 1.0;
    size_t next_ = 0;
};

} // namespace storm::ui
