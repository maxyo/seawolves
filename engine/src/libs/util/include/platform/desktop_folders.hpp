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
#include <string_view>
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

namespace detail
{
inline std::string hex64(uint64_t value)
{
    std::string bytes;
    for (int shift = 56; shift >= 0; shift -= 8)
        bytes += static_cast<char>(value >> shift);
    return ToHex(bytes);
}

// The quoted text of `#define <name> "<text>"` in a game script, trimmed of spaces; empty when there is none. A text
// scan, not the compiler: the folders are chosen before any script is compiled.
inline std::string definedText(const fs::path &script, std::string_view name)
{
    std::ifstream file(script, std::ios::binary);
    std::string line;
    while (std::getline(file, line))
    {
        const auto words = line.find_first_not_of(" \t");
        if (words == std::string::npos || line.compare(words, 7, "#define") != 0)
            continue;
        const auto at = line.find_first_not_of(" \t", words + 7);
        if (at == std::string::npos || line.compare(at, name.size(), name) != 0)
            continue;
        const auto open = line.find_first_not_of(" \t", at + name.size());
        if (open == std::string::npos || line[open] != '"')
            continue;
        const auto close = line.find('"', open + 1);
        if (close == std::string::npos)
            continue;
        const auto text = line.substr(open + 1, close - open - 1);
        const auto first = text.find_first_not_of(' ');
        return first == std::string::npos ? std::string() : text.substr(first, text.find_last_not_of(' ') - first + 1);
    }
    return {};
}
} // namespace detail

// The exact build of a game: per file of its program folder, FNV-1a 64 of its lower-case relative name, size and
// contents, summed (path_fingerprint.hpp) so the listing order does not matter; 16 hexadecimal digits. An update or a
// script mod changes it; a copy elsewhere keeps it. It keys the caches, which belong to one build.
inline std::string buildIdentity(const fs::path &game)
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
    return detail::hex64(identity);
}

// The game whatever its version: the title its scripts show (VERSION_NUMBER1 in PROGRAM/_LSC_on_off.h, the header
// the series' scripts keep their version in; «Каждому своё» and Caribbean Legend name themselves there). It keys
// the saves and the settings, which every version and script mod of the game shares. `title` is empty when the
// scripts name no title: the key is then the build identity, and the saves follow that exact build.
struct GameKey
{
    std::string key; // 16 hexadecimal digits
    std::string title;
};

inline GameKey gameKey(const fs::path &game)
{
    auto title =
        detail::definedText(storage::child(storage::child(game, "program"), "_lsc_on_off.h"), "VERSION_NUMBER1");
    if (title.empty())
        return {buildIdentity(game), {}};
    return {detail::hex64(hash::fnv1a(title)), std::move(title)};
}

// The player's folders for `game`; the layers are the engine's (installed beside it), not the game's.
inline storage::Folders gameFolders(const BaseFolders &base, const fs::path &game)
{
    if (!game.is_absolute())
        throw std::invalid_argument("The game folder must be absolute: " + game.string());
    const auto key = gameKey(game).key;
    storage::Folders folders{game,
                             base.data / kFolderName / key / "saves",
                             base.config / kFolderName / key,
                             {"options", "engine.ini"},
                             base.cache / kFolderName / buildIdentity(game),
                             {},
                             {}};
    for (const auto *root : {&folders.saves, &folders.settings, &folders.cache})
        if (storage::inside(game, *root) || storage::inside(*root, game))
            throw std::runtime_error("The player's folder " + root->string() + " and the game folder " + game.string() +
                                     " overlap: the engine never writes into the game folder");
    return folders;
}

} // namespace storm::desktop
