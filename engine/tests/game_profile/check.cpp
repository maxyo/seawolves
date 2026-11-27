// The game profile (src/libs/core/include/game_profile.hpp): which game is Caribbean Legend, and what each profile
// makes of a value the games use with different meanings.
#include "game_profile.hpp"

#include <set>
#include <string>
#include <vector>

#include <catch2/catch.hpp>

using storm::GameProfile;

namespace
{
GameProfile detect(const std::set<std::string> &paths, std::vector<std::string> &logged)
{
    return storm::detectGameProfile([&](const std::string &path) { return paths.contains(path); },
                                    [&](const std::string &line) { logged.push_back(line); });
}
} // namespace

TEST_CASE("game_profile")
{
    std::vector<std::string> logged;
    const std::set<std::string> teho{"engine.ini", "program", "resource", "program/_lsc_on_off.h"};
    REQUIRE(detect(teho, logged) == GameProfile::Legacy);
    REQUIRE(detect({}, logged) == GameProfile::Legacy); // any other game stays the legacy profile
    REQUIRE(logged.empty());

    auto cl = teho;
    cl.insert({"resource/texture_library.json", "resource/shaders"});
    REQUIRE(detect(cl, logged) == GameProfile::CaribbeanLegend);
    REQUIRE(logged.empty());

    // one marker alone is not Caribbean Legend, and it is said
    auto partial = teho;
    partial.insert("resource/shaders");
    REQUIRE(detect(partial, logged) == GameProfile::Legacy);
    REQUIRE(logged.size() == 1);
}

TEST_CASE("game_profile.colliding_ids")
{
    using storm::CollidingIdDomain;
    using storm::CollidingIdMeaning;
    using storm::collidingIdMeaning;
    // every colliding value means something else in each profile, and a value means the same in both otherwise
    for (const auto &id : storm::gameProfileFacts(GameProfile::Legacy).colliding_ids)
    {
        const auto legacy = collidingIdMeaning(GameProfile::Legacy, id.domain, id.value);
        const auto cl = collidingIdMeaning(GameProfile::CaribbeanLegend, id.domain, id.value);
        REQUIRE((legacy != CollidingIdMeaning::None && cl != CollidingIdMeaning::None && legacy != cl));
    }
    REQUIRE(collidingIdMeaning(GameProfile::Legacy, CollidingIdDomain::Message, 77012) ==
            CollidingIdMeaning::SoundResume);
    REQUIRE(collidingIdMeaning(GameProfile::CaribbeanLegend, CollidingIdDomain::Message, 77012) ==
            CollidingIdMeaning::SoundPause);
    // the same value in another domain is not a collision
    REQUIRE(collidingIdMeaning(GameProfile::CaribbeanLegend, CollidingIdDomain::SoundPlayType, 77012) ==
            CollidingIdMeaning::None);
}
