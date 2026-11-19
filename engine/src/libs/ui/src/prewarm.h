// What the first opening of a screen would make, made ahead (Host::Prewarm).
//
// A screen's first frame used to pay for its fonts and icons: RmlUi makes a font face handle for every family, style,
// weight and pixel size the first time an element needs it, loads its glyphs (ASCII with the Latin-1 supplement, then
// the 128-code-point block of every other script a text holds: rmlui-glyph-blocks.patch) and draws its layer textures,
// the font effects on the CPU; an icon is rasterised from its SVG symbol at its size and colour, its drop shadow
// blurred. That was most of the extra time of a first opening (the style, layout and render phases of "UI switch").
//
// The pack lists what its screens and HUD draw (pack.json "prewarm": {layoutWidth, fonts: [FontUse], icons: [IconUse]},
// written and checked by storm_ui_pack_check over every recorded model and the states its taps reach). Warmer makes
// them one step at a time for the game language: each font use's handle at the layout's dp ratio with the glyph blocks
// of the language's UI strings, its font effects and layer textures, then the icons' textures, so a later text or icon
// of that use costs what it costs on a second opening.
#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
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

// One icon texture a <ui-icon> of the pack is drawn with (icons.cpp): the symbol, its colour, its size in dp and its
// baked drop shadow (colour, offset and blur in dp, the faded opacity it is cast with). Colours are 0xRRGGBBAA.
struct IconUse
{
    std::string id;
    bool plain = false;
    uint32_t colour = 0;
    double size = 0.0;
    uint32_t shadow = 0;
    double x = 0.0, y = 0.0, sigma = 0.0, fade = 1.0;

    bool operator<(const IconUse &other) const
    {
        return std::tie(id, plain, colour, size, shadow, x, y, sigma, fade) <
               std::tie(other.id, other.plain, other.colour, other.size, other.shadow, other.x, other.y, other.sigma, other.fade);
    }
};
Json IconUseJson(const IconUse &use);
bool ParseIconUse(const Json &entry, IconUse &use, std::string &why);
// The icons drawn under `root` (icons.cpp).
void CollectIconUses(Rml::Element *root, float dp_ratio, std::set<IconUse> &out);
// Makes the icon's texture now, through `render` (icons.cpp; a symbol the pack lacks is logged as when drawn).
void MakeIcon(const IconUse &use, float dp_ratio, bool mark, Rml::RenderManager &render);

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
    void Reset(std::vector<FontUse> uses, std::vector<IconUse> icons, std::string sample, float dp_ratio, double narrower, bool mark);
    // Makes the next font use: handle, glyph blocks, effects, layer textures (through `render`); a step-down's handles
    // with their glyph blocks. After the fonts, the next icon textures, kIconsPerStep at a time. False when everything
    // is made. `made_ms` is what it took.
    bool Step(Rml::RenderManager &render, double &made_ms, std::string &error);
    bool Done() const
    {
        return next_ >= uses_.size() && next_icon_ >= icons_.size();
    }
    size_t Count() const
    {
        return uses_.size();
    }
    size_t Icons() const
    {
        return icons_.size();
    }
    // The next step makes icons (a few milliseconds), not a font use (up to tens on a phone).
    bool NextIsIcons() const
    {
        return next_ >= uses_.size();
    }
    static constexpr size_t kIconsPerStep = 8;
    size_t Made() const
    {
        return next_;
    }

  private:
    std::vector<FontUse> uses_;
    std::vector<IconUse> icons_;
    size_t next_icon_ = 0;
    bool mark_ = false;
    std::string sample_;
    float dp_ratio_ = 1.0f;
    double narrower_ = 1.0;
    size_t next_ = 0;
};

} // namespace storm::ui
