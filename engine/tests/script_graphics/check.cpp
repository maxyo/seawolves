#include "script_graphics.hpp"

#include <catch2/catch.hpp>

#include <map>

using namespace storm::render;

namespace
{
template <typename Set> ScriptGraphics With(Set set)
{
    ScriptGraphics graphics;
    set(graphics);
    return graphics;
}

std::map<std::string, int> Options(const ScriptGraphics &graphics)
{
    std::map<std::string, int> options;
    for (const auto &option : ScriptGraphicsOptions(graphics))
        REQUIRE(options.emplace(option.name, option.value).second);
    return options;
}
} // namespace

TEST_CASE("script_graphics.unset")
{
    // A game whose scripts never set an option (the legacy games) keeps the preset entirely.
    REQUIRE(Options({}).empty());
}

TEST_CASE("script_graphics.shadows")
{
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.shadows = 0; }))["fx_shadows"] == 0);
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.shadows = 1; }))["fx_shadows"] > 0);
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.shadows = 1; }))["fx_shadows"] <=
            Options(With([&](ScriptGraphics &g) { g.shadows = 3; }))["fx_shadows"]);
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.lamp_shadows = 0; }))["fx_lamp_shadows"] == 0);
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.ambient_occlusion = 0; }))["fx_ao_volume"] == 0);
    // Their sizes and levels are the preset's: only off is taken.
    for (int on = 1; on <= 3; ++on)
        REQUIRE(Options(With([&](ScriptGraphics &g) {
                    g.lamp_shadows = on;
                    g.ambient_occlusion = on;
                })).empty());
}

TEST_CASE("script_graphics.anti_aliasing")
{
    const auto none = Options(With([&](ScriptGraphics &g) {
        g.fxaa = 0;
        g.ssaa = 0;
    }));
    REQUIRE((none.at("fx_smaa") == 0 && none.at("fx_msaa") == 1));
    const auto post = Options(With([&](ScriptGraphics &g) {
        g.fxaa = 2;
        g.ssaa = 0;
    }));
    REQUIRE((post.at("fx_smaa") == 1 && post.at("fx_msaa") == 1));
    const auto twice = Options(With([&](ScriptGraphics &g) {
        g.fxaa = 0;
        g.ssaa = 1;
    }));
    const auto most = Options(With([&](ScriptGraphics &g) {
        g.fxaa = 0;
        g.ssaa = 3;
    }));
    REQUIRE((twice.at("fx_smaa") == 0 && twice.at("fx_msaa") > 1 && most.at("fx_msaa") > twice.at("fx_msaa")));
    // Alpha to coverage (the leaves' edges) exactly where MSAA runs.
    for (const auto *aa : {&none, &post, &twice, &most})
        REQUIRE(aa->at("fx_alpha_to_coverage") == (aa->at("fx_msaa") > 1 ? 1 : 0));
}

TEST_CASE("script_graphics.temporal")
{
    // The game's default TAA (and DLAA) is never "none": the most samples, with the leaves' edges.
    const auto taa = Options(With([&](ScriptGraphics &g) {
        g.fxaa = 0;
        g.ssaa = 0;
        g.temporal = true;
    }));
    REQUIRE((taa.at("fx_msaa") == 4 && taa.at("fx_alpha_to_coverage") == 1 && taa.at("fx_smaa") == 0));
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.temporal = true; })) == taa);
}

TEST_CASE("script_graphics.colour")
{
    const int neutral = Options(With([&](ScriptGraphics &g) { g.temperature = 0.0f; }))["fx_temperature"];
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.temperature = -1.0f; }))["fx_temperature"] < neutral);
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.temperature = 1.0f; }))["fx_temperature"] > neutral);
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.temperature = -1.0f; }))["fx_temperature"] >=
            0); // a negative option value means "the preset's"
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.saturation = 1.0f; }))["fx_saturation"] == 100);
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.saturation = 0.0f; }))["fx_saturation"] == 0);
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.saturation = 2.0f; }))["fx_saturation"] > 100);
}

TEST_CASE("script_graphics.anisotropy")
{
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.anisotropy = 0; }))["fx_anisotropy"] == 1);
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.anisotropy = 16; }))["fx_anisotropy"] == 16);
}

TEST_CASE("script_graphics.display_sizes")
{
    const std::vector<storm::DisplayMode> modes{{1920, 1080, 60}, {1280, 720, 144}, {1920, 1080, 144},
                                                {640, 480, 60},   {1024, 768, 30},  {1600, 900, 0}};
    REQUIRE(ScriptDisplaySizes(modes) == std::vector<std::pair<int, int>>{{1280, 720}, {1600, 900}, {1920, 1080}});
}
