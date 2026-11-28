#include "picture_atlas.h"
#include "numbers.h"
#include "text_utf8.h"

#include <cstdlib>
#include <vector>

namespace storm::ui
{

namespace
{

// "..\BATTLE_INTERFACE\land_icon.tga" → "BATTLE_INTERFACE/land_icon" under INTERFACES/.
std::string TextureOf(std::string name)
{
    for (char &c : name)
        if (c == '\\')
            c = '/';
    if (name.size() > 4 && Upper(name.substr(name.size() - 4)) == ".TGA")
        name.resize(name.size() - 4);
    std::vector<std::string> parts = {"INTERFACES"};
    size_t at = 0;
    while (at <= name.size())
    {
        size_t end = name.find('/', at);
        if (end == std::string::npos)
            end = name.size();
        const std::string part = name.substr(at, end - at);
        if (part == "..")
        {
            if (!parts.empty())
                parts.pop_back();
        }
        else if (!part.empty() && part != ".")
            parts.push_back(part);
        at = end + 1;
    }
    std::string out;
    for (const std::string &part : parts)
        out += (out.empty() ? "" : "/") + part;
    return out;
}

} // namespace

size_t PictureAtlas::Load(std::string_view ini)
{
    const size_t before = pictures_.size();
    std::string group, texture;
    float width = 0.0f, height = 0.0f;
    size_t at = 0;
    while (at < ini.size())
    {
        size_t end = ini.find('\n', at);
        if (end == std::string_view::npos)
            end = ini.size();
        std::string line = StripWhitespace(std::string(ini.substr(at, end - at)));
        at = end + 1;
        if (const size_t comment = line.find(';'); comment != std::string::npos)
            line = StripWhitespace(line.substr(0, comment));
        if (line.empty())
            continue;
        if (line.front() == '[')
        {
            const size_t close = line.find(']');
            group = Upper(StripWhitespace(line.substr(1, close == std::string::npos ? std::string::npos : close - 1)));
            texture.clear();
            width = height = 0.0f;
            continue;
        }
        const size_t equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        const std::string key = Upper(StripWhitespace(line.substr(0, equals)));
        const std::string value = StripWhitespace(line.substr(equals + 1));
        if (key == "STEXTURENAME")
            texture = TextureOf(value);
        else if (key == "WTEXTUREWIDTH")
            width = ParseNumber<float>(value).value_or(0.0f); // the group's pictures are then not found
        else if (key == "WTEXTUREHEIGHT")
            height = ParseNumber<float>(value).value_or(0.0f);
        else if (key == "PICTURE" && !texture.empty() && width > 0.0f && height > 0.0f)
        {
            // name,left,top,right,bottom in texture pixels
            std::vector<std::string> fields;
            size_t from = 0;
            while (from <= value.size())
            {
                size_t comma = value.find(',', from);
                if (comma == std::string::npos)
                    comma = value.size();
                fields.push_back(StripWhitespace(value.substr(from, comma - from)));
                from = comma + 1;
            }
            if (fields.size() < 5 || fields[0].empty())
                continue;
            AtlasPicture picture;
            picture.texture = texture;
            bool numbers = true;
            for (int i = 0; i < 4; ++i)
            {
                const std::optional<float> at = ParseNumber<float>(fields[i + 1]);
                numbers = numbers && at;
                picture.uv[i] = at.value_or(0.0f) / (i % 2 ? height : width);
            }
            if (numbers) // a malformed picture is not found, as one with too few fields
                pictures_.emplace(group + "/" + Upper(fields[0]), std::move(picture));
        }
    }
    return pictures_.size() - before;
}

const AtlasPicture *PictureAtlas::Find(std::string_view group, std::string_view picture) const
{
    const auto found = pictures_.find(Upper(group) + "/" + Upper(picture));
    return found == pictures_.end() ? nullptr : &found->second;
}

} // namespace storm::ui
