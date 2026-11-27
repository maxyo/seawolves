#include "script_graphics.hpp"

#include <catch2/catch.hpp>

#include <fstream>
#include <iterator>
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
    for (const auto &[name, value] : ScriptGraphicsOptions(graphics))
    {
        const GraphicsOption *option = FindGraphicsOption(name);
        REQUIRE(option);
        REQUIRE(GraphicsOptionAllows(*option, value));
        REQUIRE(options.emplace(name, value).second);
    }
    return options;
}

std::map<std::string, int> Effects(const char *option, int value, uint32_t height = 1080)
{
    std::map<std::string, int> effects;
    for (const auto &effect : GraphicsOptionEffects(*FindGraphicsOption(option), value, height))
        REQUIRE(effects.emplace(effect.name, effect.value).second);
    return effects;
}
} // namespace

TEST_CASE("script_graphics.unset")
{
    // A game whose scripts never set an option (the legacy games) leaves every option as it is.
    REQUIRE(Options({}).empty());
}

TEST_CASE("script_graphics.shadows")
{
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.shadows = 0; }))["sun_shadows"] == 0);
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.shadows = 1; }))["sun_shadows"] > 0);
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.shadows = 1; }))["sun_shadows"] <=
            Options(With([&](ScriptGraphics &g) { g.shadows = 3; }))["sun_shadows"]);
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.lamp_shadows = 0; }))["lamp_shadows"] == 0);
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.ambient_occlusion = 0; }))["ambient_occlusion"] == 0);
    // Their sizes and levels are ours: any of the game's levels is the desktop's default.
    for (int on = 1; on <= 3; ++on)
    {
        const auto options = Options(With([&](ScriptGraphics &g) {
            g.lamp_shadows = on;
            g.ambient_occlusion = on;
        }));
        REQUIRE(options.at("lamp_shadows") == FindGraphicsOption("lamp_shadows")->desktop);
        REQUIRE(options.at("ambient_occlusion") == FindGraphicsOption("ambient_occlusion")->desktop);
    }
}

TEST_CASE("script_graphics.anti_aliasing")
{
    const auto aa = [](int fxaa, int ssaa, bool temporal = false) {
        return Options(With([&](ScriptGraphics &g) {
                   g.fxaa = fxaa;
                   g.ssaa = ssaa;
                   g.temporal = temporal;
               }))
            .at("anti_aliasing");
    };
    const auto msaa = [](int value) { return Effects("anti_aliasing", value).at("fx_msaa"); };
    const auto smaa = [](int value) { return Effects("anti_aliasing", value).at("fx_smaa"); };
    REQUIRE((msaa(aa(0, 0)) == 1 && smaa(aa(0, 0)) == 0));
    REQUIRE((msaa(aa(2, 0)) == 1 && smaa(aa(2, 0)) == 1));
    REQUIRE((smaa(aa(0, 1)) == 0 && msaa(aa(0, 1)) > 1 && msaa(aa(0, 3)) > msaa(aa(0, 1))));
    // The game's default TAA (and DLAA) is never "none": the most samples.
    REQUIRE(msaa(aa(0, 0, true)) == 4);
    // Alpha to coverage (the leaves' edges) exactly where MSAA runs.
    for (const int value : FindGraphicsOption("anti_aliasing")->choices)
        REQUIRE(Effects("anti_aliasing", value).at("fx_alpha_to_coverage") == (msaa(value) > 1 ? 1 : 0));
}

TEST_CASE("script_graphics.colour")
{
    const int neutral = Options(With([&](ScriptGraphics &g) { g.temperature = 0.0f; }))["colour_temperature"];
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.temperature = -1.0f; }))["colour_temperature"] < neutral);
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.temperature = 1.0f; }))["colour_temperature"] > neutral);
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.saturation = 1.0f; }))["colour_saturation"] == 100);
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.saturation = 0.0f; }))["colour_saturation"] == 0);
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.saturation = 2.0f; }))["colour_saturation"] > 100);
}

TEST_CASE("script_graphics.anisotropy")
{
    // The game's levels 0 (off), 4, 8, 16.
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.anisotropy = 0; }))["anisotropic_filtering"] == 1);
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.anisotropy = 4; }))["anisotropic_filtering"] == 4);
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.anisotropy = 16; }))["anisotropic_filtering"] == 16);
    REQUIRE(Options(With([&](ScriptGraphics &g) { g.anisotropy = 64; }))["anisotropic_filtering"] == 16);
}

TEST_CASE("graphics_options.table")
{
    for (const GraphicsOption &option : GraphicsOptions())
    {
        INFO(option.name);
        REQUIRE(FindGraphicsOption(option.name) == &option);
        REQUIRE(GraphicsOptionAllows(option, option.desktop));
        REQUIRE(!GraphicsOptionAllows(option, option.max + 1));
        REQUIRE(!GraphicsOptionAllows(option, option.min - 1));
        REQUIRE((option.kind == OptionKind::Number) == option.choices.empty());
    }
    REQUIRE(!FindGraphicsOption("no_such_option"));
    REQUIRE(!GraphicsOptionAllows(*FindGraphicsOption("anisotropic_filtering"), 3));
}

TEST_CASE("graphics_options.render_scale")
{
    // The window's own size by default; a share of its height below 100 %.
    REQUIRE(Effects("render_scale", FindGraphicsOption("render_scale")->desktop).at("fx_render_height") == 0);
    REQUIRE(Effects("render_scale", 75, 1440).at("fx_render_height") == 1080);
}

TEST_CASE("graphics_options.desktop_full_quality")
{
    // Every effect the desktop's defaults draw is on; the texture generation is off, as on phones.
    for (const char *on : {"sun_shadows", "bloom", "sun_shafts", "lens_flare", "sea_wakes"})
        REQUIRE(FindGraphicsOption(on)->desktop == FindGraphicsOption(on)->max);
    for (const char *off : {"texture_normal_maps", "texture_height_maps", "texture_upscale"})
        REQUIRE(FindGraphicsOption(off)->desktop == 0);
    REQUIRE(Effects("anti_aliasing", FindGraphicsOption("anti_aliasing")->desktop).at("fx_msaa") == 4);
}

TEST_CASE("graphics_options.readme")
{
    // engine/README.md, "Graphics options", shows this table: on a failure paste the text below over it.
    std::ifstream file(STORM_ENGINE_README);
    const std::string readme{std::istreambuf_iterator<char>(file), {}};
    INFO(GraphicsOptionsMarkdown());
    REQUIRE(readme.find(GraphicsOptionsMarkdown()) != std::string::npos);
}

TEST_CASE("script_graphics.display_sizes")
{
    const std::vector<storm::DisplayMode> modes{{1920, 1080, 60}, {1280, 720, 144}, {1920, 1080, 144},
                                                {640, 480, 60},   {1024, 768, 30},  {1600, 900, 0}};
    REQUIRE(ScriptDisplaySizes(modes) == std::vector<std::pair<int, int>>{{1280, 720}, {1600, 900}, {1920, 1080}});
}
