// The sea AI reads the relations the game's scripts return in the scripts' own numbering (script_relations.hpp).
#include "script_relations.hpp"

#include <catch2/catch.hpp>

#include <map>
#include <string>

namespace
{
using Defines = std::map<std::string, int32_t>;

storm::sea_ai::ScriptRelations Read(const Defines &defines)
{
    return storm::sea_ai::ScriptRelations::Read([&](const char *name) -> std::optional<int32_t> {
        const auto found = defines.find(name);
        return found == defines.end() ? std::nullopt : std::optional<int32_t>(found->second);
    });
}

// The engine's decisions (fire, attack, colours) on the relation a script returned.
void RequireNumbering(const Defines &defines)
{
    const auto relations = Read(defines);
    const std::pair<const char *, uint32_t> expected[] = {{"RELATION_FRIEND", RELATION_FRIEND},
                                                          {"RELATION_NEUTRAL", RELATION_NEUTRAL},
                                                          {"RELATION_ENEMY", RELATION_ENEMY},
                                                          {"RELATION_UNKNOWN", RELATION_UNKNOWN}};
    for (const auto &[name, engine] : expected)
    {
        INFO(name);
        const int32_t script = defines.at(name);
        CHECK(relations.ToEngine(script) == engine);
        CHECK(relations.ToScript(engine) == script);
    }
}

// To Each His Own 1.7.3, RESOURCE/shared/sea_ai/Script_defines.h: an enemy is 2, which is the engine's NEUTRAL; read
// as is, no enemy ship ever fired at the player.
const Defines teho = {{"RELATION_UNKNOWN", 3}, {"RELATION_FRIEND", 0}, {"RELATION_NEUTRAL", 1}, {"RELATION_ENEMY", 2}};
} // namespace

TEST_CASE("TEHO numbering")
{
    RequireNumbering(teho);
    CHECK(Read(teho).ToEngine(2) == RELATION_ENEMY);
    CHECK_THROWS_AS(Read(teho).ToScript(RELATION_ALLIANCE), std::runtime_error);
}

TEST_CASE("the engine's own numbering, with ALLIANCE")
{
    const Defines native = {{"RELATION_UNKNOWN", 0},
                            {"RELATION_FRIEND", 1},
                            {"RELATION_NEUTRAL", 2},
                            {"RELATION_ENEMY", 3},
                            {"RELATION_ALLIANCE", 4}};
    RequireNumbering(native);
    CHECK(Read(native).ToEngine(4) == RELATION_ALLIANCE);
}

TEST_CASE("broken script headers fail loudly")
{
    Defines missing = teho;
    missing.erase("RELATION_ENEMY");
    CHECK_THROWS_AS(Read(missing), std::runtime_error);
    Defines duplicate = teho;
    duplicate["RELATION_ENEMY"] = 1;
    CHECK_THROWS_AS(Read(duplicate), std::runtime_error);
    CHECK_THROWS_AS(Read(teho).ToEngine(7), std::runtime_error);
}
