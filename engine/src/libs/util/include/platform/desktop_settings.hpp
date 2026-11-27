#pragma once

// The engine's own settings on desktop: the game folder it runs. One JSON object in the per-user settings folder.

#include "desktop_folders.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace storm::desktop
{
inline fs::path settingsFile(const BaseFolders &base)
{
    return base.config / kFolderName / "settings.json";
}

namespace detail
{
// The settings file's object; an empty one when there is no file yet.
inline nlohmann::json readSettings(const fs::path &settings)
{
    std::ifstream file(settings, std::ios::binary);
    if (!file)
    {
        if (!fs::exists(settings))
            return nlohmann::json::object();
        throw std::runtime_error("Cannot read " + settings.string());
    }
    try
    {
        auto json = nlohmann::json::parse(file);
        if (!json.is_object())
            throw std::runtime_error(settings.string() + " is not a JSON object");
        return json;
    }
    catch (const nlohmann::json::exception &error)
    {
        throw std::runtime_error(settings.string() + ": " + error.what());
    }
}
} // namespace detail

// The game folder named last, empty when none was.
inline fs::path rememberedGame(const fs::path &settings)
{
    const auto json = detail::readSettings(settings);
    const auto game = json.find("game");
    if (game == json.end())
        return {};
    if (!game->is_string())
        throw std::runtime_error(settings.string() + ": \"game\" is not a string");
    return storage::utf8Path(game->get<std::string>());
}

inline void rememberGame(const fs::path &settings, const fs::path &game)
{
    auto json = detail::readSettings(settings);
    const auto text = game.u8string();
    json["game"] = std::string(text.begin(), text.end());
    fs::create_directories(settings.parent_path());
    auto temporary = settings;
    temporary += ".storm-tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file << json.dump(2) << '\n';
        file.close();
        if (!file)
            throw std::runtime_error("Cannot write " + temporary.string());
    }
    fs::rename(temporary, settings);
}
} // namespace storm::desktop
