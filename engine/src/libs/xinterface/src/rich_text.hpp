#pragma once

// The rich-text markup of Caribbean Legend's interface texts:
//   [color=a,r,g,b] [font=NAME] [scale=1.2] [voffset=4] ... with closing tags [/color] [/font] [/scale] [/voffset]
//   [icon:group=G,pic=P,width=W,height=H,voffset=V,layer=back|inline,color=a,r,g,b]
//   \n, a line break in the text, or a line break escape `\n`
// A tag the markup does not know is reported and dropped; a `[` that starts no tag is text.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace storm::xi
{
struct RichStyle
{
    uint32_t color = 0xFFFFFFFF; // ARGB; the default is the node's colour
    bool color_set = false;
    std::string font;           // empty: the node's font
    std::optional<float> scale; // a [scale=] replaces the node's font scale; unset: the node's
    int32_t voffset = 0;
};

struct RichText
{
    std::string text;
    RichStyle style;
};

struct RichIcon
{
    std::string group;
    std::string picture;
    int32_t width = 0;
    int32_t height = 0;
    int32_t voffset = 0;
    bool back = false; // layer=back: drawn behind the text, takes no room on the line
    uint32_t color = 0xFFFFFFFF;
};

struct RichLineBreak
{
};

using RichItem = std::variant<RichText, RichIcon, RichLineBreak>;

// `report` receives every tag that is dropped
std::vector<RichItem> ParseRichText(std::string_view text, const std::function<void(std::string_view)> &report);

// What the layout needs to know about the fonts (the node knows them).
struct RichMeasure
{
    std::function<float(std::string_view, const RichStyle &)> width;
    std::function<float(const RichStyle &)> height; // of a line of that style's glyphs
};

// A piece of a laid-out line: text of item `item` (a part of it, once wrapped) or the whole icon `item`.
struct RichPiece
{
    size_t item = 0;
    std::string text;
    float x = 0, width = 0;
};

struct RichLine
{
    std::vector<RichPiece> pieces;
    float width = 0, height = 0;
};

// Lines of at most `max_width` (wrapped between words; a longer word gets a line of its own). Every line is
// `line_space` high; with `adaptive`, a line of larger glyphs grows by how much they are higher than the base style's
// and a line with a higher inline icon is as high as the icon.
std::vector<RichLine> LayoutRichText(const std::vector<RichItem> &items, float max_width, const RichMeasure &measure,
                                     float line_space, bool adaptive);

// The widest line, in pixels: `width(text, style)` measures one run (the caller knows the fonts).
int32_t RichTextWidth(const std::vector<RichItem> &items,
                      const std::function<int32_t(std::string_view, const RichStyle &)> &width);
} // namespace storm::xi
