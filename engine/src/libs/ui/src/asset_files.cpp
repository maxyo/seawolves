#include "storm_ui/asset_files.h"

#include "platform/apk_assets.hpp"

namespace storm::ui
{

namespace
{

class AssetSource final : public FileSource
{
  public:
    AssetSource(AAssetManager *assets, std::string directory) : assets_(assets), directory_(std::move(directory))
    {
    }

    bool Read(const std::string &path, std::vector<uint8_t> &out) override
    {
        if (path.find("..") != std::string::npos)
            return false;
        return storm::apk::readAsset(assets_, directory_.empty() ? path : directory_ + "/" + path, out);
    }

  private:
    AAssetManager *assets_;
    std::string directory_;
};

} // namespace

std::unique_ptr<FileSource> AssetFiles(AAssetManager *assets, std::string directory)
{
    return std::make_unique<AssetSource>(assets, std::move(directory));
}

} // namespace storm::ui
