// The interaction sessions and the height lock of a character, and the free spot search (location/src/
// character_interaction.cpp): what the seat scripts rely on when they sit a character down and stand it up.
#include "character_interaction.hpp"

#include <catch2/catch.hpp>

#include <cmath>

using storm::location::InteractionState;

TEST_CASE("one interaction session at a time, ended only by its own token")
{
    InteractionState state;
    const uint32_t token = state.Begin();
    REQUIRE(token != 0);
    CHECK(state.SessionActive());
    CHECK(state.Begin() == 0); // a second seat cannot take the character
    CHECK(!state.Transition(token + 1, true));
    CHECK(state.Transition(token, true));
    CHECK(state.TransitionLocked());
    CHECK(!state.End(0));
    CHECK(!state.End(token + 1));
    CHECK(state.SessionActive());
    CHECK(state.End(token));
    CHECK(!state.SessionActive());
    CHECK(!state.TransitionLocked()); // the lock goes with its session
    const uint32_t next = state.Begin();
    CHECK(next != 0);
    CHECK(next != token); // the old token no longer ends the new session
    CHECK(!state.End(token));
}

TEST_CASE("a height lock is released by its token or by 0, a newer lock outlives the older token")
{
    InteractionState state;
    CHECK(state.LockHeight(NAN) == 0);
    CHECK(!state.HeldHeight());
    const uint32_t first = state.LockHeight(1.5f);
    REQUIRE(first != 0);
    CHECK(state.HeldHeight() == 1.5f);
    const uint32_t second = state.LockHeight(2.0f);
    state.UnlockHeight(first); // the seat's old lock: the new one stays
    CHECK(state.HeldHeight() == 2.0f);
    state.UnlockHeight(second);
    CHECK(!state.HeldHeight());
    state.LockHeight(3.0f);
    state.UnlockHeight(0);
    CHECK(!state.HeldHeight());
}

TEST_CASE("a free spot: the centre first, then rings outwards starting towards the character")
{
    using storm::location::FindFreePositionNear;
    const glm::vec3 center(10.0f, 0.0f, 10.0f);
    const glm::vec3 character(10.0f, 0.0f, 20.0f); // to the north (+z)
    CHECK(FindFreePositionNear(center, 1.5f, character, [](glm::vec3 &) { return true; }) == center);
    // Only spots at least 0.4 from the centre are free: the first ring, its first sample towards the character.
    const auto spot =
        FindFreePositionNear(center, 1.5f, character, [&](glm::vec3 &p) { return glm::length(p - center) > 0.4f; });
    REQUIRE(spot);
    CHECK(std::fabs(glm::length(*spot - center) - 0.5f) < 1e-4f);
    CHECK(std::fabs(spot->x - center.x) < 1e-4f);
    CHECK(spot->z > center.z);
    CHECK(!FindFreePositionNear(center, 1.5f, character, [](glm::vec3 &) { return false; }));
    CHECK(!FindFreePositionNear(center, 3.5f, character, [](glm::vec3 &) { return true; }));
}
