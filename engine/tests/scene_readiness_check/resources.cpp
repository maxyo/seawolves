// Read actual producer states without a device or starting workers. This translation unit
// exposes private state only to construct the asynchronous completion ordering under test.
#include <bits/stdc++.h>
#define private public
#include "effects/lamp_shadows.hpp"
#include "effects/shadows.hpp"
#undef private
using namespace storm;
int main()
{
    effects::LampShadows lamps;
    lamps.slots_ = 1;
    lamps.prepared_ = true;
    assert(lamps.initial_state() == 0); // no light/geometry/room resource is needed
    lamps.room_.rgba.resize(4);
    assert(lamps.initial_state() == 2); // room-only upload failed after dirty was cleared
    lamps.room_dirty_ = true;
    assert(lamps.initial_state() == 1); // upload still due, not failed
    lamps.room_dirty_ = false;
    lamps.room_map_ = {1};
    lamps.active_ = true;
    lamps.static_map_ = {2};
    lamps.dynamic_map_ = {3};
    lamps.static_fb_ = {1};
    lamps.dynamic_fb_ = {2};
    lamps.layout_dirty_ = lamps.grid_dirty_ = false;
    assert(lamps.initial_state() == 0);
    lamps.grid_.rgba.resize(4);
    assert(lamps.initial_state() == 2);
    lamps.grid_map_ = {4};
    assert(lamps.initial_state() == 0);
    lamps.dynamic_fb_ = BGFX_INVALID_HANDLE;
    assert(lamps.initial_state() == 2);
    lamps.slots_ = 0;
    assert(lamps.initial_state() == 0); // disabled resources are not mandatory

    // The sun's shadows: outdoors they are required before the reveal; indoors (not applicable), in an hour without
    // shadows and without a sun above the horizon they are not.
    effects::SunShadows sun;
    fx::Settings settings;
    settings.shadows = fx::ShadowQuality::Low;
    sun.configure(settings);
    sun.set_sun({0.0f, 1.0f, 0.0f}, true, 1.0f);
    assert(sun.initial_state(false) == 0);
    assert(sun.initial_state(true) == 1); // not prepared yet
    sun.prepared_ = true;
    assert(sun.initial_state(true) == 2); // prepared, its target refused: cannot reveal
    sun.active_ = true;
    assert(sun.initial_state(true) == 0); // no casters
    sun.caster_indices_.push_back(1);
    sun.tiles_dirty_ = true;
    assert(sun.initial_state(true) == 1);
    sun.tiles_dirty_ = false;
    assert(sun.initial_state(true) == 2); // the tiles' buffer failed
    sun.tiles_vb_ = {1};
    assert(sun.initial_state(true) == 0);
    sun.tiles_vb_ = BGFX_INVALID_HANDLE;
    sun.set_sun({0.0f, 1.0f, 0.0f}, true, 0.0f);
    assert(sun.initial_state(true) == 0); // an hour without shadows (the rain clouds hide the sun) casts nothing
    sun.set_sun({0.0f, -1.0f, 0.0f}, true, 1.0f);
    assert(sun.initial_state(true) == 0); // the sun below the horizon casts nothing

    puts("actual producer initial-state chain: PASS");
}
