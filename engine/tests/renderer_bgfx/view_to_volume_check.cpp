// The volumes' view-to-texture transform (src/effects/view_to_volume.hpp): it follows a new room or rebake while the
// camera stands still, and a camera move.
#include "effects/view_to_volume.hpp"

#include <array>
#include <cmath>
#include <cstdio>

namespace
{
using Matrix = std::array<float, 16>;

bool expect(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    return ok;
}

// Row-vector matrices, as bx and the game write them.
Matrix translation(float x, float y, float z)
{
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1};
}

Matrix scale_offset(float s, float x, float y, float z)
{
    return {s, 0, 0, 0, 0, s, 0, 0, 0, 0, s, 0, x, y, z, 1};
}

bool near(const Matrix &a, const Matrix &b)
{
    for (size_t i = 0; i < a.size(); ++i)
        if (std::abs(a[i] - b[i]) > 1e-6f)
            return false;
    return true;
}
} // namespace

int main()
{
    storm::effects::ViewToVolume transform;
    const Matrix camera = translation(1, 2, 3);
    // view -> world undoes the camera's translation, then the room scales by 0.5 about the origin.
    bool ok = expect(near(transform.of(camera, scale_offset(0.5f, 0, 0, 0)), scale_offset(0.5f, -0.5f, -1, -1.5f)),
                     "the view-to-volume transform of a room");
    ok &=
        expect(near(transform.of(camera, scale_offset(0.25f, 0.1f, 0, 0)), scale_offset(0.25f, -0.15f, -0.5f, -0.75f)),
               "a new room at a camera that stands still");
    ok &= expect(
        near(transform.of(translation(0, 0, 0), scale_offset(0.25f, 0.1f, 0, 0)), scale_offset(0.25f, 0.1f, 0, 0)),
        "a camera move");
    return ok ? 0 : 1;
}
