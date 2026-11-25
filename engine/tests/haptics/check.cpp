// The haptic moments and their gaps (platform/android_haptics.hpp).
#include <platform/android_haptics.hpp>

#include <cstdlib>

#include <catch2/catch.hpp>

TEST_CASE("haptics")
{
    using namespace storm::android;
    HapticThrottle throttle;
    CHECK(throttle.Allow(Haptic::Hit, 1000));       // the first hit plays
    CHECK(!throttle.Allow(Haptic::Hit, 1100));      // a hit 100 ms later is the same hit
    CHECK(throttle.Allow(Haptic::Block, 1100));     // another kind is not held back
    CHECK(throttle.Allow(Haptic::Hit, 1250));       // a hit after its gap plays
    CHECK(throttle.Allow(Haptic::Boarding, 0));     // the first boarding plays even at time 0
    CHECK(!throttle.Allow(Haptic::Boarding, 2999)); // a boarding within 3 s is the same boarding
    CHECK((throttle.Allow(Haptic::Broadside, 5000) && !throttle.Allow(Haptic::Broadside, 5500) &&
           throttle.Allow(Haptic::Broadside, 5600))); // a broadside every 600 ms at most
    CHECK(!throttle.Allow(Haptic::Count, 9000));      // no effect for an unknown kind
}
