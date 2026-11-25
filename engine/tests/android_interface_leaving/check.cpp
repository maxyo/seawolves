// An interface whose script segment was unloaded publishes no further model (mobile_interface_leaving.hpp).
#include "mobile_interface_leaving.hpp"

#include <catch2/catch.hpp>

TEST_CASE("android_interface_leaving")
{
    storm::mobile::InterfaceLeaving leaving;
    CHECK(!leaving.Update(5, true));         // an interface whose script is loaded publishes
    CHECK(!leaving.Update(5, true));         // and keeps publishing
    CHECK(leaving.Update(5, false));         // the frame its script is gone, it publishes nothing
    CHECK(leaving.Update(5, false));         // nor in the frames until the next interface
    CHECK(leaving.Update(5, true));          // a segment loaded again by something else does not bring the opening back
    CHECK(!leaving.Update(6, true));         // the next opening publishes
    CHECK(!leaving.Update(7, false));        // an opening whose script was never seen loaded is not held back
    CHECK(!leaving.Update(7, std::nullopt)); // nor one whose adapter does not say
    CHECK((!leaving.Update(8, std::nullopt) && !leaving.Update(8, true) &&
           leaving.Update(8, false))); // unknown, loaded, gone: left
}
