#pragma once

#include <algorithm>
#include <stdexcept>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

namespace storm::android::writable
{
namespace fs = std::filesystem;

inline std::string folded(std::string text)
{
    for (char &c : text)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    return text;
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

class Storage
{
  public:
    Storage(fs::path game, fs::path user) : game_(std::move(game)), user_(std::move(user))
    {
        if (!game_.is_absolute() || !user_.is_absolute())
            throw std::invalid_argument("Writable storage roots must be absolute");
        game_ = game_.lexically_normal();
        user_ = user_.lexically_normal();
    }

    // This only classifies and resolves paths; unrelated resources return nullopt.
    std::optional<fs::path> resolve(const std::string &input) const
    {
        auto path = slashPath(input);
        if (path.is_absolute())
        {
            if (const auto suffix = suffixUnder(path, user_); !suffix.empty())
                return contained(user_, suffix);
            const auto suffix = suffixUnder(path, game_);
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
        if (first == "save")
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
            return contained(user_, fs::path("SaveData") / suffix);
        }
        if (first == "options" && (suffix.empty() || suffix == "."))
            return contained(user_, "options");
        return std::nullopt;
    }

    // The game ships an options file with its default settings: it becomes the player's first one. Saves are never
    // copied: an imported game carries none (the importers take engine.ini, options, PROGRAM and RESOURCE only).
    void seedOptions() const
    {
        const auto target = contained(user_, "options");
        if (fs::exists(entryStatus(target)))
            return;
        const auto source = child(game_, "options");
        if (!fs::exists(entryStatus(source)))
            return;
        fs::create_directories(user_);
        // Written and renamed like XINTERFACE::SaveOptionsFile writes it: never a half-copied options file.
        auto temporary = target;
        temporary += ".storm-tmp";
        fs::copy_file(source, temporary, fs::copy_options::overwrite_existing);
        fs::rename(temporary, target);
    }

  private:
    fs::path game_, user_;
};
} // namespace storm::android::writable
