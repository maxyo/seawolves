#include "camera_shake.hpp"

#include <catch2/catch.hpp>

using namespace storm::render;

namespace
{
CameraShakeRequest Request(float time, int32_t fade)
{
    CameraShakeRequest request;
    request.time = time;
    request.move_frequency = 3.0f;
    request.turn_frequency = 2.0f;
    request.move_range = 0.2f;
    request.turn_range = 0.05f;
    request.fade = fade;
    return request;
}

// The largest movement over a stretch of the shake, sampled every frame of 60 per second.
float Largest(CameraShake &shake, float seconds)
{
    float largest = 0.0f;
    for (float t = 0.0f; t < seconds; t += 1.0f / 60.0f)
    {
        const CameraShakeOffset o = shake.Offset(1.0f);
        largest = std::max({largest, std::abs(o.move.x), std::abs(o.move.y), std::abs(o.turn.x) * 4.0f});
        shake.Advance(1.0f / 60.0f);
    }
    return largest;
}
} // namespace

TEST_CASE("camera_shake.fades_and_ends")
{
    CameraShake shake;
    shake.Start(Request(2.0f, 1), 7);
    const float early = Largest(shake, 0.5f);
    shake.Advance(1.0f);
    const float late = Largest(shake, 0.4f);
    REQUIRE(early > 0.0f);
    REQUIRE(early <= 0.2f * 0.5f);
    REQUIRE(late < early);
    REQUIRE(shake.Active());
    shake.Advance(0.2f);
    REQUIRE(!shake.Active());
    REQUIRE(shake.Offset(1.0f).move == glm::vec2(0.0f));
}

TEST_CASE("camera_shake.endless_and_stop")
{
    CameraShake shake;
    shake.Start(Request(-1.0f, 1), 7);
    shake.Advance(100.0f);
    REQUIRE(shake.Active());
    REQUIRE(Largest(shake, 1.0f) > 0.02f); // never fades
    shake.Start(Request(0.0f, 1), 7);
    REQUIRE(!shake.Active());
}

TEST_CASE("camera_shake.scaled")
{
    CameraShake shake;
    shake.Start(Request(-1.0f, -1), 7);
    shake.Advance(0.3f);
    REQUIRE(shake.Offset(0.0f).turn == glm::vec3(0.0f));
    REQUIRE(shake.Offset(1.0f).move != glm::vec2(0.0f));
}

TEST_CASE("camera_shake.easing")
{
    for (int32_t kind = 0; kind <= 7; ++kind)
    {
        REQUIRE(CameraShake::Ease(kind, 0.0f) == Approx(0.0f).margin(1e-6));
        REQUIRE(CameraShake::Ease(kind, 1.0f) == Approx(1.0f));
        REQUIRE(CameraShake::Ease(kind, 0.25f) <= CameraShake::Ease(kind, 0.75f));
    }
    REQUIRE(CameraShake::Ease(-1, 0.9f) == 0.0f);
}
