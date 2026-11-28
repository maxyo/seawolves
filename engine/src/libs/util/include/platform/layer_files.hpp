#pragma once

// A layer the engine lays over the game's files (the interface scripts, the engine's techniques, the script fixes),
// read in place where the build put it: the APK's assets on Android (platform/apk_assets.hpp), a folder beside the
// engine on desktop. Its manifest.json (name -> sha256, written by the packager) is its listing; engine/README.md,
// "Where files live".

#include "../fnv_hash.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace storm::layers
{
// Where a layer's files are, by the names of its manifest.
class Source
{
  public:
    virtual ~Source() = default;
    virtual std::optional<std::string> read(const std::string &name) const = 0;
    virtual std::string where(const std::string &name) const = 0; // for messages
};

class DirectorySource final : public Source
{
  public:
    explicit DirectorySource(std::filesystem::path root) : root_(std::move(root))
    {
    }

    std::optional<std::string> read(const std::string &name) const override
    {
        std::ifstream file(root_ / name, std::ios::binary);
        if (!file)
            return std::nullopt;
        std::ostringstream buffer;
        buffer << file.rdbuf();
        if (file.bad())
            return std::nullopt;
        return std::move(buffer).str();
    }

    std::string where(const std::string &name) const override
    {
        return (root_ / name).string();
    }

  private:
    std::filesystem::path root_;
};

// One layer: its manifest is read when it is made, a file only when it is read, and the caller owns the bytes.
class Layer
{
  public:
    // Throws when the manifest cannot be read: the build that made the layer is incomplete.
    Layer(std::string name, std::unique_ptr<Source> source) : name_(std::move(name)), source_(std::move(source))
    {
        const auto manifest = source_->read("manifest.json");
        if (!manifest)
            throw std::runtime_error("The " + name_ + " layer has no manifest at " + source_->where("manifest.json"));
        try
        {
            const auto parsed = nlohmann::json::parse(*manifest);
            for (auto file = parsed.begin(); file != parsed.end(); ++file)
                files_.insert(file.key());
        }
        catch (const nlohmann::json::exception &error)
        {
            throw std::runtime_error("The " + name_ + " layer's manifest " + source_->where("manifest.json") +
                                     " is unreadable: " + error.what());
        }
        fingerprint_ = hash::fnv1a(manifest->data(), manifest->size());
    }

    const std::string &name() const
    {
        return name_;
    }

    // The manifest's names: layer-relative, '/'.
    const std::set<std::string> &files() const
    {
        return files_;
    }

    bool has(const std::string &file) const
    {
        return files_.contains(file);
    }

    std::optional<std::string> read(const std::string &file) const
    {
        return has(file) ? source_->read(file) : std::nullopt;
    }

    std::string where(const std::string &file) const
    {
        return source_->where(file);
    }

    // Changes with any file of the layer: the manifest holds every file's sha256.
    uint64_t fingerprint() const
    {
        return fingerprint_;
    }

  private:
    std::string name_;
    std::unique_ptr<Source> source_;
    std::set<std::string> files_;
    uint64_t fingerprint_ = 0;
};

inline std::shared_ptr<const Layer> directory(std::string name, std::filesystem::path root)
{
    return std::make_shared<const Layer>(std::move(name), std::make_unique<DirectorySource>(std::move(root)));
}

// The resource index entry of a layer's file: not a path any file system holds.
inline std::string indexPath(const Layer &layer, const std::string &file)
{
    return "<" + layer.name() + ">/" + file;
}
} // namespace storm::layers
