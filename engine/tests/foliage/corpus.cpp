// Every foliage file of the installed game ($STORM_GAME_DIR) through the readers: the settings, each place and each
// asset a place names. Prints the counts; fails on the first refusal or a missing asset; 77 without a game.
#include <foliage_files.hpp>
#include <selection.hpp>
#include <zip_archive.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;
using namespace storm::foliage;

namespace
{
std::string Lower(std::string text)
{
    for (char &c : text)
        c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return text;
}

std::string ReadText(const fs::path &path)
{
    std::ifstream file(path, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}
} // namespace

int main()
{
    const char *game = std::getenv("STORM_GAME_DIR");
    if (!game || !fs::is_directory(fs::path(game) / "Resource" / "Models" / "Foliage"))
    {
        std::puts("NOT_RUN: no game with foliage in $STORM_GAME_DIR");
        return 77;
    }
    const fs::path resource = fs::path(game) / "Resource";
    std::string error;
    if (!ReadSettings(ReadText(resource / "INI" / "foliage.json"), error))
    {
        std::printf("FAIL foliage.json %s\n", error.c_str());
        return 1;
    }
    std::map<std::string, fs::path> asset_files;
    for (const auto &entry : fs::directory_iterator(resource / "Models" / "Foliage"))
        asset_files["foliage/" + Lower(entry.path().filename().string())] = entry.path();

    std::map<std::string, Asset> assets;
    size_t places = 0, trees = 0;
    for (const auto &entry : fs::recursive_directory_iterator(resource / "Models"))
    {
        if (Lower(entry.path().extension().string()) != ".foliage")
            continue;
        const auto place = ReadPlace(ReadText(entry.path()), error);
        if (!place)
        {
            std::printf("FAIL %s %s\n", entry.path().c_str(), error.c_str());
            return 1;
        }
        uint64_t near_triangles = 0;
        for (const std::string &name : place->assets)
        {
            if (!assets.contains(name))
            {
                const auto file = asset_files.find(name);
                if (file == asset_files.end())
                {
                    std::printf("FAIL %s names %s, which the game lacks\n", entry.path().c_str(), name.c_str());
                    return 1;
                }
                const auto archive = storm::zip::Archive::Open(file->second, error);
                auto asset = archive ? ReadAsset(*archive, error) : std::nullopt;
                if (!asset)
                {
                    std::printf("FAIL %s %s\n", file->second.c_str(), error.c_str());
                    return 1;
                }
                assets.emplace(name, std::move(*asset));
            }
        }
        for (const Instance &instance : place->instances)
        {
            for (const Mesh &mesh : assets.at(place->assets[instance.asset]).meshes)
                near_triangles += mesh.lod == 0 ? mesh.indices.size() / 3 : 0;
        }
        const Cells cells = BuildCells(*place, 64.f);
        std::printf("%-70s %7zu trees %2zu assets %6.2f M tris at level 0, %u cells of 64 m\n",
                    fs::relative(entry.path(), resource).c_str(), place->instances.size(), place->assets.size(),
                    near_triangles / 1e6, cells.columns * cells.rows);
        ++places;
        trees += place->instances.size();
    }
    for (const auto &[name, asset] : assets)
    {
        std::printf("%-36s %u levels:", name.c_str(), asset.lods);
        for (uint32_t lod = 0; lod < asset.lods; ++lod)
        {
            size_t triangles = 0;
            for (const Mesh &mesh : asset.meshes)
                triangles += mesh.lod == lod ? mesh.indices.size() / 3 : 0;
            std::printf(" %zu", triangles);
        }
        std::printf(" triangles, %zu textures\n", asset.textures.size());
    }
    std::printf("PASS %zu places, %zu trees, %zu assets\n", places, trees, assets.size());
    return places > 0 ? 0 : 1;
}
