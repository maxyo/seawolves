#include <platform/android_land_commands.hpp>
#include <iostream>

#include <catch2/catch.hpp>
using namespace storm::android;
static storm::ui::hud::MenuStep action(const LandCommandsState &s, const char *verb, const char *id = "")
{
    return {storm::ui::hud::MenuStep::Menu::Land, s.revision(), verb, id};
}
static LandCommandsFrame frame()
{
    LandCommandsFrame f;
    f.available = true;
    f.active = true;
    f.ownerIdentity = "actual-native-list";
    f.selectedItemId = "i0";
    f.items = {{"i0", "command", "Talk", "BI_DialogStart", "target:actual-character", true, 1},
               {"i1", "cancel", "Cancel", "", "cancel", true, 1}};
    return f;
}
TEST_CASE("android_land_commands.missing_source")
{
    LandCommandsState s;
    // Record the native Close boundary: stock was opened independently and the
    // Android adapter has never supplied a usable snapshot or taken ownership.
    bool nativeActive = true;
    int nativeCloses = 0;
    const auto unavailable = [&](LandCommandSource source) {
        if (s.releaseUnavailableSource(source))
        {
            nativeActive = false;
            ++nativeCloses;
        }
    };
    unavailable(LandCommandSource::Missing);
    REQUIRE((nativeActive && nativeCloses == 0 && !s.frame().available));
    unavailable(LandCommandSource::Missing);
    REQUIRE((nativeActive && nativeCloses == 0));
    // The helper appears; its disappearance must release only Android authority (queued and
    // in-flight taps), not close the native tree.
    s.update("scene", frame());
    auto oldAction = action(s, "activateItem", "i0");
    REQUIRE(s.offer(oldAction));
    unavailable(LandCommandSource::Missing);
    REQUIRE((nativeActive && nativeCloses == 0));
    REQUIRE((!s.take() && !s.offer(oldAction)));
    // A missing helper after an executed Android operation: nothing of it revives.
    s.update("scene", frame());
    REQUIRE(s.offer(action(s, "selectItem", "i0")));
    auto select = s.take();
    REQUIRE(select);
    s.invalidate();
    s.update("scene", frame());
    oldAction = action(s, "activateItem", "i0");
    unavailable(LandCommandSource::Missing);
    REQUIRE((nativeActive && nativeCloses == 0 && !s.offer(oldAction)));
    // Recovery gives a fresh owner. No disappeared session's tokens revive.
    s.update("scene", frame());
    REQUIRE(!s.offer(oldAction));
    // Explicit original gameplay prohibition is deliberately different from
    // absence of presentation support: this result requests native Close.
    oldAction = action(s, "activateItem", "i0");
    unavailable(LandCommandSource::Forbidden);
    REQUIRE((!nativeActive && nativeCloses == 1 && !s.offer(oldAction)));
    nativeActive = true;
    s.update("scene", frame());
    REQUIRE((!s.releaseUnavailableSource(LandCommandSource::Ready) && nativeCloses == 1));
    std::cout << "PASS: a missing adapter preserves native stock; queued and in-flight taps revoked; recovery fenced; "
                 "explicit source prohibition requests Close\n";
}
