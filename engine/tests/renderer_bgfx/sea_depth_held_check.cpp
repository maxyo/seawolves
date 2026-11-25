// Which captured depth the sea reads (src/effects/sea_depth_held.hpp): only the one of this frame, of the target it is
// drawn into and of the depth that target writes; a failed capture none; and only a depth the backend can read.
#include "effects/sea_depth_held.hpp"

#include <cstdio>

using namespace storm;

namespace
{
bool expect(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    return ok;
}
} // namespace

int main()
{
    const int scene = 0, reflection = 0, scene_depth = 0, reflection_depth = 0;
    effects::SeaDepthHeld held;
    bool ok = expect(!held.holds(1, &scene, &scene_depth), "nothing captured: nothing held");
    held.captured(true, 1, &scene, &scene_depth);
    ok &= expect(held.holds(1, &scene, &scene_depth), "the capture serves the frame's further sea draws");
    ok &= expect(!held.holds(1, &reflection, &reflection_depth), "another target: captured again");
    ok &= expect(!held.holds(1, &reflection, &scene_depth), "another target sharing the depth: captured again");
    ok &= expect(!held.holds(1, &scene, &reflection_depth), "another depth of the target: captured again");
    ok &= expect(!held.holds(2, &scene, &scene_depth), "the next frame: captured again");
    held.captured(false, 2, &scene, &scene_depth);
    ok &= expect(!held.holds(2, &scene, &scene_depth), "a failed capture holds nothing: the next draw tries again");

    shim::TexStore color, depth;
    ok &= expect(effects::SeaDepthReadable(&color, &depth, false), "a plain target's depth is read");
    ok &= expect(!effects::SeaDepthReadable(&color, nullptr, true), "a target without depth: none");
    ok &= expect(!effects::SeaDepthReadable(nullptr, &depth, true), "no target: none");
    depth.target_flags = BGFX_TEXTURE_RT_MSAA_X4;
    ok &= expect(!effects::SeaDepthReadable(&color, &depth, false), "multisampled where depth is not resolved: none");
    ok &= expect(effects::SeaDepthReadable(&color, &depth, true), "multisampled where it is resolved: read");
    depth.target_flags = BGFX_TEXTURE_RT_WRITE_ONLY;
    ok &= expect(!effects::SeaDepthReadable(&color, &depth, true), "a write-only depth: none");
    return ok ? 0 : 1;
}
