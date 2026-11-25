// The scope that suppresses the native interface's GPU submissions (platform/android_interface_draw.hpp): it nests,
// is restored on unwinding and holds for its own thread only.
#include <platform/android_interface_draw.hpp>

#include <catch2/catch.hpp>

#include <stdexcept>
#include <thread>

using storm::android::NativeInterfaceDrawScope;
using storm::android::NativeInterfaceDrawSuppressed;

TEST_CASE("a suppressing scope covers its nested scopes and ends with them")
{
    REQUIRE_FALSE(NativeInterfaceDrawSuppressed());
    {
        NativeInterfaceDrawScope scope(true);
        CHECK(NativeInterfaceDrawSuppressed());
        {
            NativeInterfaceDrawScope nested(false);
            CHECK(NativeInterfaceDrawSuppressed());
        }
        CHECK(NativeInterfaceDrawSuppressed());
    }
    CHECK_FALSE(NativeInterfaceDrawSuppressed());
    {
        NativeInterfaceDrawScope scope(false);
        CHECK_FALSE(NativeInterfaceDrawSuppressed());
    }
}

TEST_CASE("the world drawn on another thread is not suppressed")
{
    NativeInterfaceDrawScope scope(true);
    bool other = true;
    std::thread([&] { other = NativeInterfaceDrawSuppressed(); }).join();
    CHECK_FALSE(other);
    CHECK(NativeInterfaceDrawSuppressed());
}

TEST_CASE("an exception out of the interface's draw ends the suppression")
{
    try
    {
        NativeInterfaceDrawScope scope(true);
        throw std::runtime_error("draw");
    }
    catch (const std::runtime_error &)
    {
    }
    CHECK_FALSE(NativeInterfaceDrawSuppressed());
}
