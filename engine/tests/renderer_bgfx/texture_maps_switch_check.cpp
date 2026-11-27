// A game that ships its own texture maps (render::EffectsGame) gets none made: with «Обработка текстур» all on, no
// preset makes relief, height or gloss maps or the neural ×2 textures, which the same options make for a game that
// generates them. No GPU: the settings are activated as before a device exists.
#include "effects/effects.hpp"
#include "storm/fx/settings.hpp"
#include "storm/render/effects.hpp"

#include <catch2/catch.hpp>

using namespace storm;

namespace
{
const std::vector<render::EffectsOption> kAllOn{
    {"texture_normal_maps", 1}, {"texture_height_maps", 1}, {"texture_upscale", 1}};

fx::BackgroundProducts ProductsOf(bool generates, int preset)
{
    effects::Host &host = effects::Host::instance();
    host.set_game({.generates_texture_maps = generates});
    host.configure(preset, kAllOn);
    return fx::BackgroundProductsFor(host.settings());
}
} // namespace

TEST_CASE("a game that generates its texture maps gets them with the switches on")
{
    const fx::BackgroundProducts high = ProductsOf(true, 2);
    CHECK(high.relief);
    CHECK(high.heights);
    CHECK(high.gloss);
    CHECK(high.upscale);
    CHECK(ProductsOf(true, 0).relief);
}

TEST_CASE("a game that ships its texture maps gets none made, on any preset")
{
    for (const int preset : {0, 1, 2})
    {
        const fx::BackgroundProducts products = ProductsOf(false, preset);
        CHECK_FALSE(products.relief);
        CHECK_FALSE(products.heights);
        CHECK_FALSE(products.gloss);
        CHECK_FALSE(products.upscale);
    }
    effects::Host::instance().set_game({});
}
