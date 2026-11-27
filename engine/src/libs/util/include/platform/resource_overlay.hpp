#pragma once

#include "../istring.hpp"
#include "storage.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace storm::overlay
{
// Only UI source/configuration is eligible. Never redirect models, textures,
// audio, saves, engine settings or arbitrary executable files into this layer.
inline bool isUiOverlayPath(const std::filesystem::path &relative)
{
    if (relative.is_absolute())
        return false;
    for (const auto &part : relative)
        if (part == "..")
            return false;
    const auto key = AsciiLower(relative.generic_string());
    const auto extension = AsciiLower(relative.extension().generic_string());
    return ((key.starts_with("program/interface/") || key.starts_with("program/controls/")) &&
            (extension == ".c" || extension == ".h")) ||
           (key.starts_with("resource/ini/interfaces/") && extension == ".ini");
}

inline bool isEngineShaderOverlayPath(const std::filesystem::path &relative)
{
    if (relative.is_absolute())
        return false;
    for (const auto &part : relative)
        if (part == "..")
            return false;
    const auto key = AsciiLower(relative.generic_string());
    const auto extension = AsciiLower(relative.extension().generic_string());
    return key.starts_with("resource/techniques/") && (extension == ".fx" || extension == ".h" || extension == ".sha");
}

// The layers that change the scripts of the program tree; part of the compiled script cache key
// (FILE_SERVICE::GetPathFingerprint).
inline std::vector<std::filesystem::path> programLayers(const storage::Layers &layers)
{
    std::vector<std::filesystem::path> result;
    for (const auto *layer : {&layers.mobileUi, &layers.scriptPatches})
        if (!layer->empty())
            result.push_back(*layer);
    return result;
}

// Returns the number of files indexed.
inline size_t indexUiOverlay(const std::filesystem::path &root, std::unordered_map<std::string, std::string> &paths,
                             bool shaders = false)
{
    size_t indexed = 0;
    if (!std::filesystem::is_directory(root))
        return indexed;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
    {
        if (entry.is_symlink() || !entry.is_regular_file())
            continue;
        const auto relative = entry.path().lexically_relative(root);
        if (shaders ? isEngineShaderOverlayPath(relative) : isUiOverlayPath(relative))
        {
            paths[AsciiLower(relative.generic_string())] = entry.path().string();
            ++indexed;
        }
    }
    return indexed;
}
} // namespace storm::overlay
