#include "platform/apk_assets.hpp"
#include "platform/layer_files.hpp"

#include <fstream>

#include <catch2/catch.hpp>

namespace fs = std::filesystem;

namespace
{
void put(const fs::path &path, const std::string &text)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}
} // namespace

TEST_CASE("layer_files")
{
    // assets/ of an APK: the layer under its own name, as the packager writes it.
    const auto assets = fs::temp_directory_path() / "layer-files";
    const auto root = assets / "mobile-ui";
    fs::remove_all(assets);
    put(root / "PROGRAM/interface/mainmenu.c", "// mobile");
    put(root / "PROGRAM/interface/stray.c", "// not the layer's");
    put(root / "manifest.json", R"({"PROGRAM/interface/mainmenu.c": "a", "PROGRAM/interface/gone.c": "b"})");

    const auto folder = storm::layers::directory("mobile-ui", root);
    AAssetManager manager{assets};
    const auto apk = storm::apk::layer(&manager, "mobile-ui");
    for (const auto &layer : {folder, apk})
    {
        REQUIRE(layer->files() == std::set<std::string>{"PROGRAM/interface/gone.c", "PROGRAM/interface/mainmenu.c"});
        REQUIRE(layer->read("PROGRAM/interface/mainmenu.c") == "// mobile");
        // The manifest is the listing: a file it does not name is not the layer's, one it names but the source lacks
        // is absent.
        REQUIRE(!layer->read("PROGRAM/interface/stray.c"));
        REQUIRE(!layer->read("PROGRAM/interface/gone.c"));
    }
    REQUIRE(apk->where("manifest.json") == "assets/mobile-ui/manifest.json");
    REQUIRE(apk->fingerprint() == folder->fingerprint());

    // The fingerprint follows the manifest, which holds every file's sha256.
    put(root / "manifest.json", R"({"PROGRAM/interface/mainmenu.c": "c", "PROGRAM/interface/gone.c": "b"})");
    REQUIRE(storm::layers::directory("mobile-ui", root)->fingerprint() != folder->fingerprint());

    // A layer without its manifest, or with one that is not a listing, is an incomplete build: refused, never empty.
    put(root / "manifest.json", "[");
    REQUIRE_THROWS_WITH(storm::layers::directory("mobile-ui", root), Catch::Contains("unreadable"));
    fs::remove(root / "manifest.json");
    REQUIRE_THROWS_WITH(storm::layers::directory("mobile-ui", root), Catch::Contains("no manifest"));
    REQUIRE_THROWS_WITH(storm::apk::layer(&manager, "mobile-ui"), Catch::Contains("assets/mobile-ui/manifest.json"));
    fs::remove_all(assets);
}
