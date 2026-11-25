// The land menu's source (android_land_source.hpp): what the scripts' AndroidLandRefresh wrote is taken only when the
// event ran without a script error and wrote a complete, valid version 1; anything else leaves no metadata behind.
#include "android_land_source.hpp"
#include "core_fake.hpp"

#include <catch2/catch.hpp>

#include <functional>

using storm::android::LandCommandSource;

namespace
{
struct Land
{
    storm::test::Codec codec;
    ATTRIBUTES root{codec};
    // What the scripts' handler does; by default it writes a complete version 1.
    std::function<void()> handler = [this] { write(); };

    ATTRIBUTES &write()
    {
        auto &meta = root.CreateAttribute("AndroidLand");
        for (const auto &[name, value] : {std::pair{"dialog", "-1"},
                                          {"officer", "1"},
                                          {"body", "-1"},
                                          {"reload", ""},
                                          {"pendingAllowed", "1"},
                                          {"herePicture", "-1"},
                                          {"hereTexture", "-1"},
                                          {"hereLabel", ""},
                                          {"version", "1"}})
            meta.SetAttribute(name, value);
        return meta;
    }
    LandCommandSource refresh()
    {
        return storm::android::refreshLandSource(&root, [this] { handler(); });
    }
    bool metadataLeft()
    {
        return root.GetAttributeClass("AndroidLand") != nullptr;
    }
};
} // namespace

TEST_CASE("a complete version 1 is the source; pendingAllowed 0 forbids it")
{
    Land land;
    CHECK(land.refresh() == LandCommandSource::Ready);
    land.handler = [&] { land.write().SetAttribute("pendingAllowed", "0"); };
    CHECK(land.refresh() == LandCommandSource::Forbidden);
}

TEST_CASE("the previous refresh's metadata is never inherited")
{
    Land land;
    REQUIRE(land.refresh() == LandCommandSource::Ready);
    land.handler = [] {}; // the handler is gone before its first statement
    CHECK(land.refresh() == LandCommandSource::Missing);
    CHECK_FALSE(land.metadataLeft());
}

TEST_CASE("a field missing leaves nothing behind")
{
    for (const char *field : {"version", "dialog", "officer", "body", "reload", "pendingAllowed", "herePicture",
                              "hereTexture", "hereLabel"})
    {
        CAPTURE(field);
        Land land;
        land.handler = [&] {
            auto &meta = land.write();
            meta.DeleteAttributeClassX(meta.GetAttributeClass(field));
        };
        CHECK(land.refresh() == LandCommandSource::Missing);
        CHECK_FALSE(land.metadataLeft());
    }
}

TEST_CASE("malformed fields are refused")
{
    const auto with = [](const char *field, const char *value) {
        Land land;
        land.handler = [&] { land.write().SetAttribute(field, value); };
        return land.refresh();
    };
    for (const char *index : {"", "-2", "2147483648", "12junk", "x"})
        for (const char *field : {"dialog", "officer", "body"})
        {
            CAPTURE(index, field);
            CHECK(with(field, index) == LandCommandSource::Missing);
        }
    CHECK(with("dialog", "1234") == LandCommandSource::Ready);
    CHECK(with("version", "2") == LandCommandSource::Missing);
    CHECK(with("pendingAllowed", "true") == LandCommandSource::Missing);
    CHECK(with("reload", "bad\nlocator") == LandCommandSource::Missing);
}

TEST_CASE("the hero's place: a picture and its texture together or neither, its name valid text")
{
    const auto here = [](const char *picture, const char *texture, const char *label) {
        Land land;
        land.handler = [&] {
            auto &meta = land.write();
            meta.SetAttribute("herePicture", picture);
            meta.SetAttribute("hereTexture", texture);
            meta.SetAttribute("hereLabel", label);
        };
        return land.refresh();
    };
    CHECK(here("1", "1", "Верфь") == LandCommandSource::Ready);
    CHECK(here("1", "1", "") == LandCommandSource::Ready); // no note: the travel stage is incomplete, not refused
    CHECK(here("1", "-1", "Верфь") == LandCommandSource::Missing);
    CHECK(here("-1", "1", "") == LandCommandSource::Missing);
    CHECK(here("x", "1", "Верфь") == LandCommandSource::Missing);
    CHECK(here("1", "1", "bad\nname") == LandCommandSource::Missing);
}

TEST_CASE("a script error in the event refuses everything it wrote")
{
    Land land;
    SECTION("before the metadata")
    {
        land.handler = [&] {
            ++storm::android::scriptErrorEpoch;
            land.write();
        };
    }
    SECTION("after the metadata")
    {
        land.handler = [&] {
            land.write();
            ++storm::android::scriptErrorEpoch;
        };
    }
    CHECK(land.refresh() == LandCommandSource::Missing);
    CHECK_FALSE(land.metadataLeft());
    land.handler = [&] { land.write(); };
    CHECK(land.refresh() == LandCommandSource::Ready); // an earlier error does not poison a clean event
}

TEST_CASE("a missing source releases the touch menu only")
{
    storm::android::LandCommandsState state;
    storm::android::LandCommandsFrame frame;
    frame.available = frame.active = true;
    frame.ownerIdentity = "list";
    frame.selectedItemId = "i0";
    frame.items = {{"i0", "command", "Talk", "BI_DialogStart", "target", true, 1, ""},
                   {"i1", "cancel", "Cancel", "", "cancel", true, 1, ""}};
    state.update("scene", frame);
    const storm::ui::hud::MenuStep tap{storm::ui::hud::MenuStep::Menu::Land, state.revision(), "activateItem", "i0"};
    REQUIRE(state.offer(tap));
    Land land;
    land.handler = [&] {
        land.write();
        ++storm::android::scriptErrorEpoch;
    };
    CHECK_FALSE(state.releaseUnavailableSource(land.refresh())); // the native menu is not closed
    CHECK_FALSE(state.take());
    CHECK_FALSE(state.offer(tap));
    land.handler = [&] { land.write(); };
    REQUIRE(land.refresh() == LandCommandSource::Ready);
    state.update("scene", frame);
    CHECK_FALSE(state.offer(tap));
}
