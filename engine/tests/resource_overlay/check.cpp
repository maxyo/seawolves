#include "platform/resource_overlay.hpp"
#include <fstream>

#include <catch2/catch.hpp>
TEST_CASE("resource_overlay")
{
    namespace fs = std::filesystem;
    using namespace storm::overlay;
    const fs::path root = fs::temp_directory_path() / "resource-overlay";
    fs::remove_all(root);
    fs::create_directories(root / "PROGRAM/interface");
    std::ofstream(root / "PROGRAM/interface/mainmenu.c") << "// mobile";
    std::ofstream(root / "manifest.json")
        << R"({"PROGRAM/interface/mainmenu.c": "a", "RESOURCE/MODELS/excluded.gm": "b",
        "RESOURCE/techniques/sea.fx": "c", "SOURCE.json": "d"})";
    const auto layer = storm::layers::directory("mobile-ui", root);
    REQUIRE(layerKeys(*layer, isUiOverlayPath) ==
            std::map<std::string, std::string>{{"program/interface/mainmenu.c", "PROGRAM/interface/mainmenu.c"}});
    REQUIRE(layerKeys(*layer, isEngineShaderOverlayPath) ==
            std::map<std::string, std::string>{{"resource/techniques/sea.fx", "RESOURCE/techniques/sea.fx"}});
    REQUIRE(!isUiOverlayPath("../program/interface/mainmenu.c"));
    REQUIRE(!isUiOverlayPath("/program/interface/mainmenu.c"));
    REQUIRE(!isUiOverlayPath("PROGRAM/interface/setup.exe"));
    REQUIRE(isUiOverlayPath("RESOURCE/INI/INTERFACES/mainmenu.ini"));

    std::unordered_map<std::string, std::string> paths{{"program/interface/mainmenu.c", "original"}};
    storm::layers::Files files;
    lay(layer, "program/interface/mainmenu.c", "PROGRAM/interface/mainmenu.c", paths, files);
    const auto &file = files.at(paths.at("program/interface/mainmenu.c"));
    REQUIRE(file.layer->read(file.name) == "// mobile");
}
