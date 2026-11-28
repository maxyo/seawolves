#pragma once
// The NDK's asset manager for engine code built for a host check: a directory stands in for the APK's assets.
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/types.h>

struct AAssetManager
{
    std::filesystem::path root; // what the APK's assets/ holds
};

struct AAsset
{
    std::string data;
    size_t read = 0;
};

enum
{
    AASSET_MODE_UNKNOWN = 0,
    AASSET_MODE_BUFFER = 3,
};

inline AAsset *AAssetManager_open(AAssetManager *manager, const char *name, int)
{
    std::ifstream file(manager->root / name, std::ios::binary);
    if (!file)
        return nullptr;
    return new AAsset{std::string(std::istreambuf_iterator<char>(file), {}), 0};
}

inline off64_t AAsset_getLength64(AAsset *asset)
{
    return static_cast<off64_t>(asset->data.size());
}

inline int AAsset_read(AAsset *asset, void *buffer, size_t count)
{
    const auto n = std::min(count, asset->data.size() - asset->read);
    std::copy_n(asset->data.data() + asset->read, n, static_cast<char *>(buffer));
    asset->read += n;
    return static_cast<int>(n);
}

inline void AAsset_close(AAsset *asset)
{
    delete asset;
}
