#pragma once

// The engine's own settings on desktop: the game folder it runs and what the player says of himself. One JSON object
// in the per-user settings folder.

#include "desktop_folders.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

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

// The player's name ("player_name", empty when unset) and what he owns of the store ("owned_apps": the app ids of
// add-ons and games the scripts ask about, none when unset). The engine has no store to ask: only the player can say.
struct PlayerSettings
{
    std::string name;
    std::vector<int32_t> ownedApps;
};

inline PlayerSettings playerSettings(const fs::path &settings)
{
    const auto json = detail::readSettings(settings);
    PlayerSettings player;
    if (const auto name = json.find("player_name"); name != json.end())
    {
        if (!name->is_string())
            throw std::runtime_error(settings.string() + ": \"player_name\" is not a string");
        player.name = name->get<std::string>();
    }
    if (const auto owned = json.find("owned_apps"); owned != json.end())
    {
        if (!owned->is_array())
            throw std::runtime_error(settings.string() + ": \"owned_apps\" is not a list of app ids");
        for (const auto &id : *owned)
        {
            if (!id.is_number_integer() || id.get<int64_t>() != static_cast<int32_t>(id.get<int64_t>()))
                throw std::runtime_error(settings.string() + ": \"owned_apps\" holds " + id.dump() + ", not an app id");
            player.ownedApps.push_back(static_cast<int32_t>(id.get<int64_t>()));
        }
    }
    return player;
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
