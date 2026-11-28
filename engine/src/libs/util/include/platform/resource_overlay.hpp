#pragma once

#include "../istring.hpp"
#include "layer_files.hpp"
#include "storage.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>

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

// The resource index entries of a layer's files a filter (isUiOverlayPath, isEngineShaderOverlayPath) admits:
// key -> the file's name in the layer.
template <class Admits> std::map<std::string, std::string> layerKeys(const layers::Layer &layer, Admits &&admits)
{
    std::map<std::string, std::string> keys;
    for (const auto &file : layer.files())
        if (admits(std::filesystem::path(file)))
            keys.emplace(AsciiLower(file), file);
    return keys;
}

// Points the resource index key `key` at `file` of `layer`.
inline void lay(const std::shared_ptr<const layers::Layer> &layer, const std::string &key, const std::string &file,
                std::unordered_map<std::string, std::string> &paths, layers::Files &files)
{
    auto entry = layers::indexPath(*layer, file);
    paths[key] = entry;
    files[std::move(entry)] = {layer, file};
}
} // namespace storm::overlay
