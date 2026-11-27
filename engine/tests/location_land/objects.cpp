// Object containers (location/src/object_library.hpp): the object library's file and the expansion of a container's
// locators into props. "[corpus]" reads the library of the installed game in $STORM_GAME_DIR.
#include "object_library.hpp"

#include "istring.hpp"

#include <catch2/catch.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>

using namespace storm::location;

namespace
{
glm::mat4 At(float x, float y, float z)
{
    glm::mat4 m(1.0f);
    m[3] = glm::vec4(x, y, z, 1.0f);
    return m;
}
} // namespace

TEST_CASE("the object library maps groups to models, any case")
{
    const auto library = ParseObjectLibrary(R"({"Crate": "crate_big", "lamp": "Lamp_02"})");
    REQUIRE(library.valid);
    CHECK(library.errors.empty());
    CHECK(library.models.size() == 2);
    const ContainerLocator locators[] = {{"CRATE", "c1", At(1, 0, 0)}, {"Lamp", "l1", At(0, 2, 0)}};
    const auto expansion = ExpandContainer(locators, library);
    REQUIRE(expansion.instances.size() == 2);
    CHECK(expansion.instances[0].model == "crate_big");
    CHECK(expansion.instances[1].model == "Lamp_02");
    CHECK(expansion.instances[1].transform[3][1] == 2.0f);
    CHECK(expansion.problems.empty());
}

TEST_CASE("entries the library cannot use are reported and left out, the rest is kept")
{
    const auto library = ParseObjectLibrary(R"({"a": "m1", "A": "m2", "b": 3, "": "m4", "c": "", "d": "m5"})");
    REQUIRE(library.valid);
    CHECK(library.models.size() == 1);
    CHECK(library.models.count("d") == 1);
    CHECK(library.errors.size() == 5); // a and A, b, the empty group, the empty model
}

TEST_CASE("a library that is not a JSON object is refused")
{
    CHECK_FALSE(ParseObjectLibrary("{\"a\": ").valid);
    CHECK_FALSE(ParseObjectLibrary("[\"a\"]").valid);
    CHECK_FALSE(ParseObjectLibrary("[\"a\"]").errors.empty());
}

TEST_CASE("a container places one prop per locator of a known group, nothing else")
{
    const auto library = ParseObjectLibrary(R"({"barrel": "barrel_01"})");
    glm::mat4 broken = At(0, 0, 0);
    broken[3][0] = std::numeric_limits<float>::quiet_NaN();
    const ContainerLocator locators[] = {
        {"barrel", "b1", At(1, 0, 0)}, {"barrel", "b2", At(2, 0, 0)}, {"barrel", "B1", At(3, 0, 0)},
        {"barrel", "b3", broken},      {"rope", "r1", At(4, 0, 0)},   {"rope", "r2", At(5, 0, 0)},
    };
    const auto expansion = ExpandContainer(locators, library);
    REQUIRE(expansion.instances.size() == 2);
    CHECK(expansion.instances[0].locator == "b1");
    CHECK(expansion.instances[1].transform[3][0] == 2.0f);
    // the duplicate b1, the non-finite b3 and the unknown group once
    CHECK(expansion.problems.size() == 3);
}

TEST_CASE("the object library of an installed game", "[.corpus]")
{
    namespace fs = std::filesystem;
    const char *root = std::getenv("STORM_GAME_DIR");
    if (!root || !*root)
    {
        std::puts("NOT_RUN: set STORM_GAME_DIR to an installed game");
        std::exit(77);
    }
    fs::path file, models;
    for (const auto &entry : fs::recursive_directory_iterator(root))
    {
        const auto relative = storm::AsciiLower(fs::relative(entry.path(), root).generic_string());
        if (relative == "resource/ini/location_objects.json")
            file = entry.path();
        else if (relative == "resource/models/object")
            models = entry.path();
    }
    if (file.empty())
    {
        std::puts("NOT_RUN: this game has no resource/ini/location_objects.json");
        std::exit(77);
    }
    std::ifstream in(file, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto library = ParseObjectLibrary(text);
    REQUIRE(library.valid);
    for (const auto &error : library.errors)
        std::printf("library: %s\n", error.c_str());
    CHECK(library.errors.empty());
    REQUIRE_FALSE(models.empty());
    std::set<std::string> present;
    for (const auto &entry : fs::directory_iterator(models))
        present.insert(storm::AsciiLower(entry.path().filename().string()));
    int missing = 0;
    for (const auto &[group, model] : library.models)
        if (!present.contains(storm::AsciiLower(model + ".gm")))
        {
            std::printf("model of group %s is missing: %s.gm\n", group.c_str(), model.c_str());
            ++missing;
        }
    std::printf("%zu object definitions, %d without a model file\n", library.models.size(), missing);
    CHECK(missing == 0);
}
