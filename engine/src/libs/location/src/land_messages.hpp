// Arguments of land messages whose layout differs between games, read with the real MESSAGE where the
// message carries it; a layout that matches neither stays a MessageFormatError.
#pragma once

#include "message.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>

namespace storm::location
{

// The optional trailing long at `position` of the format (ADD_MODEL's dynamic lights, TieItem's keep flag,
// TELEPORT_TO_LOCATOR's force), read when the message has it.
inline std::optional<int32_t> ReadOptionalLong(MESSAGE &message, size_t position)
{
    if (message.GetFormat().size() <= position)
        return std::nullopt;
    return message.Long();
}

// MSG_CHARACTER_VIEWSTRPOS "fffsfff" after the code: offset x, z, y from the head, text, red, green, blue.
struct ViewText
{
    glm::vec3 offset; // added to the head position
    std::string text;
    uint32_t color; // 0xRRGGBB
};

inline ViewText ReadViewText(MESSAGE &message)
{
    ViewText view;
    view.offset.x = message.Float();
    view.offset.z = message.Float();
    view.offset.y = message.Float();
    view.text = message.String();
    view.color = 0;
    for (int channel = 0; channel < 3; ++channel)
        view.color = (view.color << 8) | static_cast<uint32_t>(std::clamp(message.Float(), 0.0f, 255.0f));
    return view;
}

// SetSex: To Each His Own sends 1 for a man and 0 otherwise, Caribbean Legend 1 man, 2 woman, 3 skeleton.
inline bool ReadIsMale(MESSAGE &message)
{
    return message.Long() == 1;
}

} // namespace storm::location
