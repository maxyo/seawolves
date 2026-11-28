#pragma once
// What the game's save list reads of a record: its description, and its picture as a texture.
#include "core.h"
#include "renderer.h"
#include "save_thumbnail.hpp"
#include "platform/jni_text.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <set>
#include <string>

namespace storm
{
// A record's description as text for the interface: valid UTF-8 that keeps every valid character of it (jni_text
// RepairUtf8). The saved bytes are unchanged; a repair is logged once per record.
inline std::string SaveDescriptionText(const save::thumbnail::Extension &extension, std::string_view record)
{
    std::string description(extension.description);
    if (const size_t replaced = jni_text::RepairUtf8(description))
    {
        static std::set<std::string> reported;
        if (reported.emplace(record).second)
            spdlog::warn("Save '{}': {} bytes of its description are not UTF-8 and are shown as U+FFFD", record,
                         replaced);
    }
    return description;
}

// The description of the save `saveName` into `buf` (MSG_INTERFACE_GET_SAVE_DATA, the save list), made valid UTF-8 and
// cut at a code point; false when the record has no readable description or `buf` holds only the terminator.
inline bool ReadSaveDescription(const char *saveName, int32_t bufSize, char *buf)
{
    if (buf == nullptr || bufSize <= 0)
        return false;
    buf[0] = 0;
    if (saveName == nullptr)
        return false;
    int32_t allDatSize = 0;
    std::unique_ptr<char[]> data(static_cast<char *>(core.GetSaveData(saveName, allDatSize)));
    if (!data || allDatSize <= 0)
        return false;
    const auto extension =
        save::thumbnail::Decode({reinterpret_cast<const uint8_t *>(data.get()), static_cast<size_t>(allDatSize)});
    if (!extension.valid)
    {
        spdlog::error("Save '{}' has no readable description: it cannot be listed or loaded", saveName);
        return false;
    }
    if (extension.picture_problem)
        spdlog::error("Save '{}': {}: shown without its picture, still loadable", saveName, extension.picture_problem);

    const std::string description = SaveDescriptionText(extension, saveName);
    size_t count = std::min(description.size(), static_cast<size_t>(bufSize - 1));
    // A small script buffer must not cut a UTF-8 code point in half.
    while (count < description.size() && count > 0 && (static_cast<unsigned char>(description[count]) & 0xc0) == 0x80)
        --count;
    std::memcpy(buf, description.data(), count);
    buf[count] = 0;
    // As before the decoder: a record with a description (even an empty one) reads, unless the buffer holds only
    // the terminator.
    return bufSize > 1;
}

// The record's description and its picture as a texture of the renderer `rs` (MSG_SCRSHOT_READ). The game's save list
// (save_load.c procLoadOneSaveInfo / ShowDataForSave) takes a record without a description for "corrupted", and makes
// a slot selectable only when it gets a picture texture. A record may legitimately carry no picture
// (SFLB_DoSaveFileData writes one when no frame could be shot): it keeps its description and gets an empty picture, so
// the game lists, selects and loads it as it does every other record. A record without a readable description gets
// neither (corrupted).
template <class Renderer> int32_t SaveRecordTexture(Renderer *rs, const char *fileName, char **pDatStr)
{
    using save::thumbnail::kHeight, save::thumbnail::kWidth;
    *pDatStr = nullptr;
    int32_t datSize = 0;
    const std::unique_ptr<char[]> pdat(static_cast<char *>(core.GetSaveData(fileName, datSize)));
    if (!pdat || datSize <= 0)
        return -1;
    const auto extension =
        save::thumbnail::Decode({reinterpret_cast<const uint8_t *>(pdat.get()), static_cast<size_t>(datSize)});
    if (!extension.valid)
    {
        spdlog::error("Save '{}' has no readable description: the game lists it as corrupted", fileName);
        return -1;
    }
    if (extension.picture_problem)
        spdlog::error("Save '{}': {}: listed with an empty picture, still loadable", fileName,
                      extension.picture_problem);

    int32_t textureId = rs->TextureCreate(kWidth, kHeight, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED);
    D3DLOCKED_RECT outRect;
    if (textureId < 0 || !rs->TextureLock(textureId, 0, outRect, 0))
    {
        spdlog::error("Save '{}': its picture texture cannot be made: the game lists it as corrupted", fileName);
        TEXTURE_RELEASE(rs, textureId);
        return -1;
    }
    for (uint32_t row = 0; row < kHeight; ++row)
    {
        auto *const out = static_cast<uint8_t *>(outRect.pBits) + static_cast<size_t>(outRect.Pitch) * row;
        if (extension.pixels.empty())
            std::memset(out, 0, kWidth * 4);
        else
            std::memcpy(out, extension.pixels.data() + static_cast<size_t>(row) * kWidth * 4, kWidth * 4);
    }
    rs->TextureUnlock(textureId, 0);

    const std::string description = SaveDescriptionText(extension, fileName);
    *pDatStr = new char[description.size() + 1];
    std::memcpy(*pDatStr, description.c_str(), description.size() + 1);
    return textureId;
}
} // namespace storm
