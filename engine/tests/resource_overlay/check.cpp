#include "platform/resource_overlay.hpp"
#include <fstream>

#include <catch2/catch.hpp>
TEST_CASE("resource_overlay")
{
    namespace fs = std::filesystem;
    using namespace storm::overlay;
    const fs::path root = fs::temp_directory_path();
    fs::create_directories(root / "PROGRAM/interface");
    fs::create_directories(root / "RESOURCE/MODELS");
    std::ofstream(root / "PROGRAM/interface/mainmenu.c") << "// mobile";
    std::ofstream(root / "RESOURCE/MODELS/excluded.gm") << "not eligible";
    std::unordered_map<std::string, std::string> paths{{"program/interface/mainmenu.c", "original"},
                                                       {"resource/models/original.gm", "user asset"}};
    indexUiOverlay(root, paths);
    REQUIRE(paths.at("program/interface/mainmenu.c") == (root / "PROGRAM/interface/mainmenu.c").string());
    REQUIRE(paths.at("resource/models/original.gm") == "user asset");
    REQUIRE(!paths.contains("resource/models/excluded.gm"));
    REQUIRE(!isUiOverlayPath("../program/interface/mainmenu.c"));
    REQUIRE(!isUiOverlayPath("/program/interface/mainmenu.c"));
    REQUIRE(!isUiOverlayPath("PROGRAM/interface/setup.exe"));
    REQUIRE(isUiOverlayPath("RESOURCE/INI/INTERFACES/mainmenu.ini"));
    fs::create_symlink(root / "RESOURCE/MODELS/excluded.gm", root / "PROGRAM/interface/link.c");
    indexUiOverlay(root, paths);
    REQUIRE(!paths.contains("program/interface/link.c"));
}
