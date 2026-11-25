// The ball splashes' shared buffers and textures (src/libs/ball_splash/src/t_splash.cpp): created once for all the
// splashes of a scene, released with the last of them, and created again with the renderer of the next scene or the
// next engine run, never through the renderer that is gone.
#include "t_splash.h"

#include <catch2/catch.hpp>

#include <memory>

namespace
{
void Cohort(VRenderer &renderer)
{
    const int before = renderer.created;
    auto first = std::make_unique<TSplash>();
    auto last = std::make_unique<TSplash>();
    first->Initialize(nullptr, nullptr, &renderer);
    last->Initialize(nullptr, nullptr, &renderer);
    CHECK(renderer.created == before + 6); // one shared set, not one per splash
    first.reset();
    CHECK(renderer.released == before); // the other splash still uses them
    last.reset();
    CHECK(renderer.released == renderer.created);
}
} // namespace

TEST_CASE("scene reloads and a renderer replaced between engine runs")
{
    auto first = std::make_unique<VRenderer>();
    // The replacement exists before the old one goes: their addresses differ.
    auto second = std::make_unique<VRenderer>();
    Cohort(*first);
    Cohort(*first);
    first.reset();
    Cohort(*second);
    Cohort(*second);
    second.reset();
    auto third = std::make_unique<VRenderer>();
    Cohort(*third);
}
