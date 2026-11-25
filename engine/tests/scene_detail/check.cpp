// The preset's grass and sea detail (src/libs/renderer/src/scene_detail.hpp).
#include "scene_detail.hpp"

#include <cstdlib>

#include <catch2/catch.hpp>

TEST_CASE("scene_detail")
{
    using namespace storm::render;
    for (int preset = -1; preset <= 3; ++preset)
    {
        const SceneDetail detail = PresetSceneDetail(preset);
        CHECK((detail.grass >= 0 && detail.grass <= 3));   // grass is one of the game's four qualities
        CHECK((detail.sea >= 0.0f && detail.sea <= 1.0f)); // sea detail is in the game's 0..1 range
    }
    CHECK((PresetSceneDetail(2).grass == 0 &&
           SameSeaDetail(PresetSceneDetail(2).sea, 1.0f))); // Высокая keeps the game's defaults
    CHECK((PresetSceneDetail(1).grass == 0 &&
           SameSeaDetail(PresetSceneDetail(1).sea, 1.0f))); // Средняя keeps the game's defaults
    CHECK((PresetSceneDetail(0).grass > PresetSceneDetail(1).grass &&
           PresetSceneDetail(0).sea < PresetSceneDetail(1).sea));         // Низкая is lighter than Средняя
    CHECK(PresetSceneDetail(7).grass == PresetSceneDetail(2).grass);      // out-of-range presets clamp
    CHECK((SameSeaDetail(0.5f, 0.5004f) && !SameSeaDetail(0.5f, 0.51f))); // sea detail compares with a small tolerance
}
