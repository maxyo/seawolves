#pragma once

// The desktop's folders: the game folder the player names and, per game, the player's own folders below the
// platform's per-user folders. engine/README.md, "Where files live".

#include "../fnv_hash.hpp"
#include "../hex.hpp"
#include "../istring.hpp"
#include "path_fingerprint.hpp"
#include "storage.hpp"

#include <cstdint>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace storm::desktop
{
namespace fs = std::filesystem;

struct BaseFolders
{
    fs::path config; // settings
    fs::path data;   // saves
    fs::path cache;  // what can be made again
    fs::path state;  // logs
};

inline constexpr char kFolderName[] = "seawolves";

// `environment` reads a variable: nullptr when it is not set.
inline BaseFolders baseFolders(const std::function<const char *(const char *)> &environment)
{
#if defined(__linux__)
    // The XDG Base Directory Specification: an unset or empty variable means its default below $HOME.
    const auto folder = [&](const char *variable, const char *belowHome) {
        if (const char *value = environment(variable); value && *value)
        {
            const auto path = storage::utf8Path(value);
            if (!path.is_absolute())
                throw std::runtime_error(std::string(variable) + " is not an absolute path: " + value);
            return path;
        }
        const char *home = environment("HOME");
        if (!home || !*home)
            throw std::runtime_error(std::string("Neither ") + variable + " nor HOME is set");
        return storage::utf8Path(home) / belowHome;
    };
    return {folder("XDG_CONFIG_HOME", ".config"), folder("XDG_DATA_HOME", ".local/share"),
            folder("XDG_CACHE_HOME", ".cache"), folder("XDG_STATE_HOME", ".local/state")};
#else
#error "storm::desktop::baseFolders has no per-user folders for this platform"
#endif
}

inline fs::path logsFolder(const BaseFolders &base)
{
    return base.state / kFolderName / "logs";
}

// The game's identity is the scripts it runs: per file of its program folder, FNV-1a 64 of its lower-case relative
// name, size and contents, summed (path_fingerprint.hpp) so the listing order does not matter; 16 hexadecimal digits.
// Two games or two versions of one game differ in it; the same game moved or copied elsewhere keeps it, and with it its
// saves.
inline std::string gameIdentity(const fs::path &game)
{
    const auto program = storage::child(game, "program");
    if (!fs::is_directory(program))
        throw std::runtime_error(game.string() + " has no PROGRAM folder: it is not a game folder");
    uint64_t identity = 0;
    std::vector<char> buffer(1 << 20);
    for (const auto &entry : fs::recursive_directory_iterator(program))
    {
        if (!entry.is_regular_file())
            continue;
        const auto name = AsciiLower(entry.path().lexically_relative(program).generic_string());
        uint64_t hash = hash::fnvRound(hash::fnv1a(name), entry.file_size());
        std::ifstream file(entry.path(), std::ios::binary);
        while (file.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || file.gcount() > 0)
            hash = hash::fnv1a(buffer.data(), static_cast<size_t>(file.gcount()), hash);
        if (!file.eof())
            throw std::runtime_error("Cannot read " + entry.path().string());
        identity += fingerprint::mix(hash);
    }
    std::string bytes;
    for (int shift = 56; shift >= 0; shift -= 8)
        bytes += static_cast<char>(identity >> shift);
    return ToHex(bytes);
}

// `engine`: the engine's own folder, which holds the layers installed with it.
inline storage::Folders gameFolders(const BaseFolders &base, const fs::path &game, const fs::path &engine)
{
    if (!game.is_absolute())
        throw std::invalid_argument("The game folder must be absolute: " + game.string());
    const auto identity = gameIdentity(game);
    const auto layers = engine / "layers";
    storage::Folders folders{game,
                             base.data / kFolderName / identity / "saves",
                             base.config / kFolderName / identity,
                             {"options", "engine.ini"},
                             base.cache / kFolderName / identity,
                             {{}, layers / "engine-shaders", layers / "script-patches"}};
    for (const auto *root : {&folders.saves, &folders.settings, &folders.cache})
        if (storage::inside(game, *root) || storage::inside(*root, game))
            throw std::runtime_error("The player's folder " + root->string() + " and the game folder " + game.string() +
                                     " overlap: the engine never writes into the game folder");
    for (const auto *layer : {&folders.layers.engineShaders, &folders.layers.scriptPatches})
        if (!fs::is_directory(*layer))
            throw std::runtime_error("The engine's " + layer->string() + " is missing: the installation is incomplete");
    return folders;
}

} // namespace storm::desktop
