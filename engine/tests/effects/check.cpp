// The effect table (libs/effects effect_table.hpp), the placement the PLAY messages give (effect_messages.hpp) and the
// angles of an effect created along a direction (storm_glm.h); "[corpus]" reads the table of the installed game in
// $STORM_GAME_DIR.
#include "effect_messages.hpp"
#include "effect_table.hpp"

#include "istring.hpp"
#include "shared/messages.h"
#include "storm_glm.h"

#include <catch2/catch.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>

using storm::effects::EffectEntry;
using storm::effects::ParseEffectTable;

TEST_CASE("the effect table keeps Effekseer effects and particle systems, ids in any case")
{
    const auto table = ParseEffectTable(R"({
        "_____LOCATOR_____": "",
        "locator_torch_p": {"file": "CLTorchFire.efkefc", "speed": 1.0},
        "mainmenu_fire": {"file": "Fire.XPS", "speed": 0.5}
    })");
    CHECK(table.errors.empty());
    REQUIRE(table.entries.size() == 2);
    const EffectEntry *torch = table.Find("Locator_Torch_P");
    REQUIRE(torch);
    CHECK(torch->kind == EffectEntry::Kind::Effekseer);
    CHECK(torch->EffectPath() == "resource/effects/CLTorchFire.efkefc");
    const EffectEntry *fire = table.Find("mainmenu_fire");
    REQUIRE(fire);
    CHECK(fire->kind == EffectEntry::Kind::ParticleSystem);
    CHECK(fire->speed == 0.5f);
    CHECK(table.Find("_____LOCATOR_____") == nullptr);
    CHECK(table.Find("torch") == nullptr);
}

TEST_CASE("entries the engine cannot play are left out by name")
{
    const auto table = ParseEffectTable(R"({
        "sound": {"file": "boom.wav", "speed": 1.0},
        "no_speed": {"file": "a.efkefc"},
        "text_speed": {"file": "a.efkefc", "speed": "fast"},
        "extra": {"file": "a.efkefc", "speed": 1.0, "loop": true},
        "bare": "a.efkefc",
        "Same": {"file": "a.efkefc", "speed": 1.0},
        "same": {"file": "b.efkefc", "speed": 1.0}
    })");
    CHECK(table.errors.size() == 6);
    REQUIRE(table.entries.size() == 1);
    CHECK(table.Find("same")->file == "a.efkefc");
    CHECK(ParseEffectTable("[1, 2]").errors.size() == 1);
    CHECK(ParseEffectTable("{ broken").errors.size() == 1);
}

TEST_CASE("PLAY places an effect with the parts its message gives, and refuses other lengths")
{
    const auto read = [](MESSAGE &message) {
        REQUIRE(message.Long() == MSG_EFFECT_PLAY);
        REQUIRE(message.String() == "fire");
        return storm::effects::ReadPlacement(message);
    };
    MESSAGE position;
    position.Reset("lsfff", int32_t{MSG_EFFECT_PLAY}, std::string("fire"), 1.0f, 2.0f, 3.0f);
    const auto placed = read(position);
    REQUIRE(placed);
    CHECK(placed->position.y == 2.0f);
    CHECK(placed->angles.y == 0.0f);
    CHECK(placed->scale.z == 1.0f);
    MESSAGE complex;
    complex.Reset("lsfffffffffl", int32_t{MSG_EFFECT_PLAY}, std::string("fire"), 1.0f, 2.0f, 3.0f, 0.1f, 0.2f, 0.3f,
                  2.0f, 3.0f, 4.0f, 30);
    const auto full = read(complex);
    REQUIRE(full);
    CHECK(full->angles.z == 0.3f);
    CHECK(full->scale.x == 2.0f);
    CHECK(full->start_frame == 30);
    MESSAGE torn;
    torn.Reset("lsffff", int32_t{MSG_EFFECT_PLAY}, std::string("fire"), 1.0f, 2.0f, 3.0f, 4.0f);
    CHECK_FALSE(read(torn));
}

TEST_CASE("an effect created along a direction is turned towards it")
{
    // Along +Z: no turn. Along +X: a quarter turn of heading. Straight down: the pitch turns +Z down.
    CHECK(storm::math::directionAngles({0, 0, 1}) == glm::vec3(0, 0, 0));
    const glm::vec3 east = storm::math::directionAngles({1, 0, 0});
    CHECK(east.y == Approx(1.5707964f));
    CHECK(east.x == 0.0f);
    const glm::vec3 down = storm::math::directionAngles({0, -1, 0});
    CHECK(down.x == Approx(1.5707964f));
    CHECK(storm::math::directionAngles({0, 0, 0}) == glm::vec3(0, 0, 0));
}

TEST_CASE("the effect table of an installed game", "[.corpus]")
{
    namespace fs = std::filesystem;
    const char *root = std::getenv("STORM_GAME_DIR");
    if (!root || !*root)
    {
        std::puts("NOT_RUN: set STORM_GAME_DIR to an installed game");
        std::exit(77);
    }
    // Every file of the game by its path in lower case.
    std::set<std::string> files;
    fs::path table_file;
    for (const auto &entry : fs::recursive_directory_iterator(root))
    {
        const std::string relative = storm::AsciiLower(fs::relative(entry.path(), root).generic_string());
        files.insert(relative);
        if (relative == "resource/effects/effects.json")
            table_file = entry.path();
    }
    if (table_file.empty())
    {
        std::puts("NOT_RUN: this game has no resource/effects/effects.json");
        std::exit(77);
    }
    std::ifstream in(table_file, std::ios::binary);
    const auto table = ParseEffectTable(std::string(std::istreambuf_iterator<char>(in), {}));
    for (const auto &error : table.errors)
        std::printf("table: %s\n", error.c_str());
    CHECK(table.errors.empty());
    CHECK_FALSE(table.entries.empty());
    // Every Effekseer effect is there; a particle system the game names without shipping it is the game's own gap
    // (the particles entity reports it when a script asks for it), listed here.
    for (const auto &[id, entry] : table.entries)
    {
        if (entry.kind == EffectEntry::Kind::Effekseer)
        {
            INFO(id << " names " << entry.EffectPath());
            CHECK(files.contains(storm::AsciiLower(entry.EffectPath())));
        }
        else if (!files.contains(storm::AsciiLower("resource/particles/" + entry.file)))
            std::printf("%s names the particle system %s, which the game does not ship\n", id.c_str(),
                        entry.file.c_str());
    }
}
