// A bone's animation between two frames (src/libs/animation/src/bone.cpp): its packed rotation keys and the root's
// position, at interior frames and at the last one, which holds.
#include "core.h"
#include "bone.h"

#include <catch2/catch.hpp>

#include <cstring>

TestCore core;

namespace
{
bool Same(const glm::quat &a, const glm::quat &b)
{
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

glm::quat Blend(Bone &bone, int32_t frame, float k)
{
    glm::quat q(4, 1, 2, 3);
    bone.BlendFrame(frame, k, q);
    return q;
}

// Keys A, A, B: the first pair is equal (its decoding is shared), the second is not.
struct Track
{
    Bone bone;
    glm::quat a = glm::normalize(glm::quat(0.9f, 0.1f, 0.3f, -0.2f));
    glm::quat b = glm::normalize(glm::quat(0.2f, -0.7f, 0.4f, 0.5f));
    Track()
    {
        glm::vec3 start(0.0f);
        bone.SetNumFrames(3, start);
        const glm::quat keys[3] = {a, a, b};
        bone.SetAngles(keys, 3);
    }
};
} // namespace

TEST_CASE("equal keys give one rotation at every blend")
{
    Track track;
    const glm::quat start = Blend(track.bone, 0, 0.0f);
    for (const float k : {0.375f, 1.0f, 1.25f})
        CHECK(Same(Blend(track.bone, 0, k), start));
}

TEST_CASE("different keys blend from one to the other")
{
    Track track;
    const glm::quat from = Blend(track.bone, 1, 0.0f), to = Blend(track.bone, 1, 1.0f);
    CHECK(Same(from, Blend(track.bone, 0, 0.0f)));
    CHECK_FALSE(Same(from, to));
    CHECK(glm::abs(glm::dot(to, track.b)) > 0.9999f);
    const glm::quat middle = Blend(track.bone, 1, 0.5f);
    CHECK_FALSE(Same(middle, from));
    CHECK_FALSE(Same(middle, to));
    // The blend is clamped to the pair.
    CHECK(Same(Blend(track.bone, 1, 1.25f), to));
    CHECK(Same(Blend(track.bone, 1, -0.25f), from));
}

TEST_CASE("the last frame holds")
{
    Track track;
    const glm::quat last = Blend(track.bone, 1, 1.0f);
    CHECK(Same(Blend(track.bone, 2, 0.5f), last));
    CHECK(Same(Blend(track.bone, 5, 0.0f), last));
}

TEST_CASE("the root's position holds at the last frame, without a read past the track")
{
    Bone bone;
    glm::vec3 start(0.0f);
    bone.SetNumFrames(3, start, true);
    const glm::vec3 positions[3] = {glm::vec3(0.0f), glm::vec3(2.0f, 4.0f, 6.0f), glm::vec3(10.0f)};
    bone.SetPositions(positions, 3);
    CHECK(bone.BlendPosition(0, 0.5f) == glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(bone.BlendPosition(1, 0.0f) == positions[1]);
    CHECK(bone.BlendPosition(2, 0.0f) == positions[2]);
    CHECK(bone.BlendPosition(2, 0.75f) == positions[2]);
    bone.SetNumFrames(1, start, true);
    bone.SetPositions(positions, 1);
    CHECK(bone.BlendPosition(0, 0.5f) == positions[0]);
}
