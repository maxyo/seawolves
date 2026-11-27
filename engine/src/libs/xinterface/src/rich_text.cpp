#include "rich_text.hpp"

#include <algorithm>
#include <charconv>
#include <optional>
#include <utility>

#include <fmt/format.h>

namespace storm::xi
{
namespace
{
struct Tag
{
    bool closing = false;
    std::string_view name;
    std::string_view value;
    size_t end = 0; // past the `]`
};

// `[name]`, `[/name]`, `[name=value]` or `[name:value]` at text[at] == '['
std::optional<Tag> ReadTag(std::string_view text, size_t at)
{
    Tag tag;
    size_t pos = at + 1;
    tag.closing = pos < text.size() && text[pos] == '/';
    pos += tag.closing;
    const size_t name_end = text.find_first_of(":=]", pos);
    if (name_end == std::string_view::npos || name_end == pos)
        return std::nullopt;
    tag.name = text.substr(pos, name_end - pos);
    if (text[name_end] == ']')
    {
        tag.end = name_end + 1;
        return tag;
    }
    const size_t close = text.find(']', name_end + 1);
    if (close == std::string_view::npos)
        return std::nullopt;
    tag.value = text.substr(name_end + 1, close - name_end - 1);
    tag.end = close + 1;
    return tag;
}

int32_t Integer(std::string_view text)
{
    while (!text.empty() && text.front() == ' ')
        text.remove_prefix(1);
    int32_t value = 0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

// "a,r,g,b" in decimal; a missing component stays 255
uint32_t Argb(std::string_view text)
{
    uint32_t parts[4] = {255, 255, 255, 255};
    for (auto &part : parts)
    {
        const size_t comma = text.find(',');
        part = static_cast<uint32_t>(std::clamp(Integer(text.substr(0, comma)), 0, 255));
        if (comma == std::string_view::npos)
            break;
        text.remove_prefix(comma + 1);
    }
    return parts[0] << 24 | parts[1] << 16 | parts[2] << 8 | parts[3];
}

// group=G,pic=P,...: the values do not contain `=`, so a piece without one continues the previous value (color=a,r,g,b)
RichIcon Icon(std::string_view arguments)
{
    std::vector<std::pair<std::string_view, std::string>> pairs;
    while (!arguments.empty())
    {
        const size_t comma = arguments.find(',');
        const std::string_view piece = arguments.substr(0, comma);
        if (const size_t equals = piece.find('='); equals != std::string_view::npos)
            pairs.emplace_back(piece.substr(0, equals), std::string(piece.substr(equals + 1)));
        else if (!pairs.empty())
            pairs.back().second.append(",").append(piece);
        if (comma == std::string_view::npos)
            break;
        arguments.remove_prefix(comma + 1);
    }
    RichIcon icon;
    for (const auto &[key, value] : pairs)
    {
        if (key == "group")
            icon.group = value;
        else if (key == "pic")
            icon.picture = value;
        else if (key == "width")
            icon.width = Integer(value);
        else if (key == "height")
            icon.height = Integer(value);
        else if (key == "voffset")
            icon.voffset = Integer(value);
        else if (key == "layer")
            icon.back = value == "back";
        else if (key == "color")
            icon.color = Argb(value);
    }
    return icon;
}
} // namespace

std::vector<RichItem> ParseRichText(std::string_view text, const std::function<void(std::string_view)> &report)
{
    std::vector<RichItem> items;
    std::vector<uint32_t> colors;
    std::vector<std::string> fonts;
    std::vector<float> scales;
    std::vector<int32_t> voffsets;
    std::string run;
    const auto style = [&] {
        RichStyle s;
        s.color_set = !colors.empty();
        if (s.color_set)
            s.color = colors.back();
        if (!fonts.empty())
            s.font = fonts.back();
        if (!scales.empty())
            s.scale = scales.back();
        if (!voffsets.empty())
            s.voffset = voffsets.back();
        return s;
    };
    const auto flush = [&] {
        if (!run.empty())
            items.emplace_back(RichText{std::exchange(run, {}), style()});
    };
    const auto pop = [](auto &stack) {
        if (!stack.empty())
            stack.pop_back();
    };

    for (size_t pos = 0; pos < text.size();)
    {
        const char c = text[pos];
        if (c == '\\' && pos + 1 < text.size() && (text[pos + 1] == 'n' || text[pos + 1] == 'N'))
        {
            flush();
            items.emplace_back(RichLineBreak{});
            pos += 2;
            continue;
        }
        if (c == '\r' || c == '\n')
        {
            flush();
            items.emplace_back(RichLineBreak{});
            const char other = c == '\r' ? '\n' : '\r';
            pos += pos + 1 < text.size() && text[pos + 1] == other ? 2 : 1;
            while (pos < text.size() && text[pos] == ' ')
                ++pos;
            continue;
        }
        const auto tag = c == '[' ? ReadTag(text, pos) : std::nullopt;
        if (!tag)
        {
            run += c;
            ++pos;
            continue;
        }
        flush();
        const size_t start = std::exchange(pos, tag->end);
        if (tag->name == "icon" && !tag->closing)
            items.emplace_back(Icon(tag->value));
        else if (tag->name == "color")
            tag->closing ? pop(colors) : colors.push_back(Argb(tag->value));
        else if (tag->name == "scale")
        {
            float scale = 1.0f;
            std::from_chars(tag->value.data(), tag->value.data() + tag->value.size(), scale);
            tag->closing ? pop(scales) : scales.push_back(scale);
        }
        else if (tag->name == "font")
            tag->closing ? pop(fonts) : fonts.push_back(std::string(tag->value));
        else if (tag->name == "voffset")
            tag->closing ? pop(voffsets) : voffsets.push_back(std::max(Integer(tag->value), 0));
        else
            report(text.substr(start, pos - start));
    }
    flush();
    return items;
}

int32_t RichTextWidth(const std::vector<RichItem> &items,
                      const std::function<int32_t(std::string_view, const RichStyle &)> &width)
{
    int32_t widest = 0;
    int32_t line = 0;
    for (const auto &item : items)
    {
        if (const auto *text = std::get_if<RichText>(&item))
            line += width(text->text, text->style);
        else if (const auto *icon = std::get_if<RichIcon>(&item))
            line += icon->back ? 0 : icon->width;
        else
            line = 0;
        widest = std::max(widest, line);
    }
    return widest;
}
} // namespace storm::xi
