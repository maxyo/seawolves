#pragma once

// The game folder and the player's own folders, one set per process: engine/README.md, "Where files live".

#include "../istring.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace storm::storage
{
namespace fs = std::filesystem;

// The layers laid over the game's files (platform/resource_overlay.hpp, platform/script_patches.hpp). An empty path:
// the platform has no such layer.
struct Layers
{
    fs::path mobileUi;      // whole interface scripts and their ini files
    fs::path engineShaders; // the engine's techniques
    fs::path scriptPatches; // fixes to the game's scripts
};

struct Folders
{
    fs::path game;                          // the player's game: read, never written
    fs::path saves;                         // what the game's scripts call its "save" folder
    fs::path settings;                      // the game's settings files named below
    std::vector<std::string> settingsFiles; // lower-case names of files in the game's root kept in `settings`
    fs::path cache;                         // everything the engine derives and can make again
    Layers layers;
};

// A UTF-8 path from text (std::filesystem::u8path is deprecated; a plain string is not UTF-8 on every platform).
inline fs::path utf8Path(std::string_view text)
{
    return fs::path(std::u8string(text.begin(), text.end()));
}

inline std::string folded(std::string text)
{
    return AsciiLower(std::move(text));
}

inline fs::path slashPath(std::string text)
{
    std::replace(text.begin(), text.end(), '\\', '/');
    // Android/POSIX native paths are UTF-8 byte strings.
    return fs::path(text);
}

inline fs::path suffixUnder(const fs::path &path, const fs::path &root)
{
    auto p = path.begin();
    for (const auto &component : root)
        if (p == path.end() || *p++ != component)
            return {};
    fs::path suffix;
    for (; p != path.end(); ++p)
        suffix /= *p;
    return suffix.empty() ? fs::path(".") : suffix;
}

inline fs::file_status entryStatus(const fs::path &path)
{
    std::error_code error;
    auto result = fs::symlink_status(path, error);
    if (error && error != std::errc::no_such_file_or_directory)
        throw fs::filesystem_error("Inspect writable path", path, error);
    return result;
}

// Emulate the game's ASCII-insensitive paths, rejecting ambiguous duplicates.
// Never traverse a symlink, including when its exact spelling matches.
inline fs::path child(const fs::path &parent, const fs::path &name)
{
    const auto desired = folded(name.string());
    fs::path result = parent / name;
    bool matched = false;
    if (fs::is_directory(entryStatus(parent)))
    {
        for (const auto &entry : fs::directory_iterator(parent))
        {
            if (folded(entry.path().filename().string()) != desired)
                continue;
            if (matched)
                throw fs::filesystem_error("Ambiguous save name", parent / name,
                                           std::make_error_code(std::errc::file_exists));
            result = entry.path();
            matched = true;
        }
    }
    if (fs::is_symlink(entryStatus(result)))
        throw fs::filesystem_error("Symlink in save path", result,
                                   std::make_error_code(std::errc::too_many_symbolic_link_levels));
    return result;
}

inline fs::path contained(const fs::path &root, const fs::path &suffix)
{
    if (suffix.is_absolute())
        throw fs::filesystem_error("Save path escapes private storage", suffix,
                                   std::make_error_code(std::errc::permission_denied));
    if (fs::is_symlink(entryStatus(root)))
        throw fs::filesystem_error("Symlink at private storage root", root,
                                   std::make_error_code(std::errc::too_many_symbolic_link_levels));
    auto result = root;
    for (const auto &component : suffix)
    {
        if (component == "." || component.empty())
            continue;
        if (component == "..")
        {
            if (result == root)
                throw fs::filesystem_error("Save path escapes private storage", suffix,
                                           std::make_error_code(std::errc::permission_denied));
            result = result.parent_path();
        }
        else
            result = child(result, component);
    }
    return result;
}

// `path` is `root` or below it, after symbolic links and "..". False for an empty root.
inline bool inside(const fs::path &root, const fs::path &path)
{
    std::error_code error;
    const auto relative = fs::weakly_canonical(path, error).lexically_relative(fs::weakly_canonical(root, error));
    return !error && !relative.empty() && *relative.begin() != "..";
}

// The only places the engine writes through the file service: the saves, the settings and the cache.
inline bool writable(const Folders &folders, const fs::path &path)
{
    return inside(folders.saves, path) || inside(folders.settings, path) || inside(folders.cache, path);
}

// Maps the game's names of its saves and settings files to the player's folders.
class Storage
{
  public:
    explicit Storage(Folders folders) : folders_(std::move(folders))
    {
        if (!folders_.game.is_absolute())
            throw std::invalid_argument("The game folder must be absolute");
        for (auto *root : {&folders_.saves, &folders_.settings})
        {
            if (!root->empty() && !root->is_absolute())
                throw std::invalid_argument("Writable storage roots must be absolute");
            *root = root->lexically_normal();
        }
        folders_.game = folders_.game.lexically_normal();
    }

    // This only classifies and resolves paths; unrelated resources return nullopt.
    std::optional<fs::path> resolve(const std::string &input) const
    {
        auto path = slashPath(input);
        if (path.is_absolute())
        {
            for (const auto *root : {&folders_.saves, &folders_.settings})
                if (const auto suffix = root->empty() ? fs::path() : suffixUnder(path, *root); !suffix.empty())
                    return contained(*root, suffix);
            const auto suffix = suffixUnder(path, folders_.game);
            if (suffix.empty())
                return std::nullopt;
            path = suffix;
        }
        auto it = path.begin();
        while (it != path.end() && (*it == "." || it->empty()))
            ++it;
        if (it == path.end())
            return std::nullopt;
        const auto first = folded(it->string());
        fs::path suffix;
        for (++it; it != path.end(); ++it)
            suffix /= *it;
        if (first == "save" && !folders_.saves.empty())
        {
            int depth = 0;
            for (const auto &part : suffix)
            {
                if (part == "..")
                    --depth;
                else if (part != "." && !part.empty())
                    ++depth;
                if (depth < 0)
                    throw fs::filesystem_error("Save path escapes SaveData", path,
                                               std::make_error_code(std::errc::permission_denied));
            }
            return contained(folders_.saves, suffix);
        }
        if (isSettingsFile(first) && (suffix.empty() || suffix == "."))
            return contained(folders_.settings, first);
        return std::nullopt;
    }

    // The game ships its settings files with their defaults: each becomes the player's first one. Saves are never
    // copied: the game's own "save" folder is not read.
    void seedSettings() const
    {
        for (const auto &name : folders_.settingsFiles)
        {
            const auto target = contained(folders_.settings, name);
            if (fs::exists(entryStatus(target)))
                continue;
            const auto source = child(folders_.game, name);
            if (!fs::exists(entryStatus(source)))
                continue;
            fs::create_directories(folders_.settings);
            // Written and renamed like XINTERFACE::SaveOptionsFile writes it: never a half-copied settings file.
            auto temporary = target;
            temporary += ".storm-tmp";
            fs::copy_file(source, temporary, fs::copy_options::overwrite_existing);
            // The copy keeps the source's mode: the game's file may be read only, the player's must not be.
            fs::permissions(temporary, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::add);
            fs::rename(temporary, target);
        }
    }

  private:
    bool isSettingsFile(const std::string &name) const
    {
        return std::find(folders_.settingsFiles.begin(), folders_.settingsFiles.end(), name) !=
               folders_.settingsFiles.end();
    }

    Folders folders_;
};

namespace detail
{
inline std::optional<Folders> &chosen()
{
    static std::optional<Folders> folders;
    return folders;
}
} // namespace detail

// The process's folders, chosen at start-up before the game's files are used (Android: storm::android::folders,
// desktop: storm::desktop::gameFolders). A tool may choose again before it scans another game.
inline void select(Folders folders)
{
    detail::chosen() = std::move(folders);
}

inline const Folders &folders()
{
    if (!detail::chosen())
        throw std::logic_error("The game's folders are used before the game is chosen");
    return *detail::chosen();
}
} // namespace storm::storage
