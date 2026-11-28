#pragma once

// The UI overlay (the application's interface layer) over the game's resource index, where a file of the layer
// replaces one of the game's bound to that file's version as a script patch is (script_patches.hpp).

#include "platform/script_patches.hpp"

#include <nlohmann/json.hpp>

namespace storm::script_patches
{
struct OverlayReport
{
    std::set<std::string> overlaid;   // the keys of the resource index the layer's files now answer
    std::vector<std::string> skipped; // log lines: the replacements not used, and why
};

// Lays the UI overlay `layer` over the resource index `paths`, its files in `files`. A file of the layer the game has
// too replaces the game's own: like a patch's target it is written for one version of that file and used only over it.
// The layer's SOURCE.json names each replacement ("adapted", its path in the layer) with the sha256 of the game's file
// it was written for ("game_sha256"); over any other file, or unnamed, the game's own file stays. Files the game does
// not have are added as they are. Throws LayerError when SOURCE.json cannot be read as that list.
inline OverlayReport applyUiOverlay(const std::shared_ptr<const layers::Layer> &layer,
                                    std::unordered_map<std::string, std::string> &paths, layers::Files &files)
{
    OverlayReport report;
    // No layer (the desktop build without the touch interface): no SOURCE.json of the working directory either.
    if (!layer)
        return report;
    std::map<std::string, std::string> written; // key -> sha256 of the game's file it replaces
    if (const auto text = layer->read("SOURCE.json"))
        try
        {
            for (const auto &file : nlohmann::json::parse(*text).value("files", nlohmann::json::array()))
                if (file.contains("game_sha256"))
                    written[AsciiLower(file.at("adapted").get<std::string>())] =
                        file.at("game_sha256").get<std::string>();
        }
        catch (const nlohmann::json::exception &error)
        {
            throw LayerError("UI layer " + layer->where("SOURCE.json") + ": " + error.what());
        }
    for (const auto &[key, file] : overlay::layerKeys(*layer, overlay::isUiOverlayPath))
    {
        if (const auto game = paths.find(key); game != paths.end())
        {
            const auto expected = written.find(key);
            std::string data;
            const std::string actual = detail::readFile(game->second, data) ? sha256(data) : "unreadable";
            if (expected == written.end() || actual != expected->second)
            {
                report.skipped.push_back("UI layer: " + key + " is not used, the game's own file is: " +
                                         (expected == written.end()
                                              ? std::string("it replaces a file of the game and SOURCE.json names no "
                                                            "version of that file it was written for")
                                              : "it was written for the game's file of sha256 " + expected->second +
                                                    ", this game's is " + actual));
                continue;
            }
        }
        report.overlaid.insert(key);
        overlay::lay(layer, key, file, paths, files);
    }
    return report;
}

} // namespace storm::script_patches
