#pragma once

// The APK's assets read in place through the NDK's AAssetManager: the layers (platform/layer_files.hpp) and the UI
// pack. Compressed entries are fine: only whole files are read.

#include "layer_files.hpp"

#include <android/asset_manager.h>

#include <memory>
#include <optional>
#include <string>

namespace storm::apk
{
// The whole asset `path` into `out` (a byte container); false when the APK has no such asset or it cannot be read.
template <class Bytes> bool readAsset(AAssetManager *assets, const std::string &path, Bytes &out)
{
    AAsset *asset = AAssetManager_open(assets, path.c_str(), AASSET_MODE_BUFFER);
    if (!asset)
        return false;
    out.resize(static_cast<size_t>(AAsset_getLength64(asset)));
    const bool read = out.empty() || AAsset_read(asset, out.data(), out.size()) == static_cast<int>(out.size());
    AAsset_close(asset);
    return read;
}

// A layer's files under the assets' `directory`. The AssetManager outlives the source (held by a global reference).
class AssetSource final : public layers::Source
{
  public:
    AssetSource(AAssetManager *assets, std::string directory) : assets_(assets), directory_(std::move(directory))
    {
    }

    std::optional<std::string> read(const std::string &name) const override
    {
        std::string data;
        return readAsset(assets_, path(name), data) ? std::optional(std::move(data)) : std::nullopt;
    }

    std::string where(const std::string &name) const override
    {
        return "assets/" + path(name);
    }

  private:
    std::string path(const std::string &name) const
    {
        return directory_.empty() ? name : directory_ + "/" + name;
    }

    AAssetManager *assets_;
    std::string directory_;
};

// The layer the APK carries as assets/<name>.
inline std::shared_ptr<const layers::Layer> layer(AAssetManager *assets, const std::string &name)
{
    return std::make_shared<const layers::Layer>(name, std::make_unique<AssetSource>(assets, name));
}
} // namespace storm::apk
