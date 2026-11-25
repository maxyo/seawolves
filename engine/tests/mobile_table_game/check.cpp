#include "mobile_table_game.hpp"

#include <catch2/catch.hpp>
using namespace storm::mobile;
static TableGameImage Image(const char *node, const char *group, const char *picture, int x, int y)
{
    TableGameImage image{node, x, y};
    image.image.source = {"atlas", group, picture, {}, {}, -1};
    return image;
}
TEST_CASE("mobile_table_game")
{
    TableGameSource source;
    source.images = {Image("HeroDice1", "DICE", "dice_3_1", 60, 486), Image("HeroDice4", "", "", 0, 0),
                     Image("PCard18", "CARDS", "blank", 400, 36)};
    source.strings = {{"Money", "7 600"}, {"Beta_TXT", "debug"}};
    source.facts = {{"rate", "200"}, {"open", "0"}, {"opponent", std::nullopt}, {"heroDice", "3,3,3,6,1"}};
    std::vector<MobileWidget> widgets;
    CollectTableGame(source, widgets);
    // The removed picture and the beta line are not exported; the marker comes last.
    REQUIRE(widgets.size() == 4);
    REQUIRE((widgets[0].node == "HeroDice1" && widgets[0].type == "image" && widgets[0].label == "dice_3_1"));
    REQUIRE((widgets[0].images.size() == 1 && widgets[0].images[0].source.group == "DICE"));
    REQUIRE(widgets[0].order == 486 * 8192 + 60);
    REQUIRE((widgets[1].node == "PCard18" && widgets[1].label == "blank"));
    REQUIRE((widgets[2].node == "Money" && widgets[2].type == "text" && widgets[2].label == "7 600"));
    const auto &marker = widgets[3];
    REQUIRE((marker.node == "ANDROID_TABLE_GAME" && marker.type == "table_game" && !marker.enabled));
    REQUIRE((marker.context.at("rate") == "200" && marker.context.at("heroDice") == "3,3,3,6,1"));
    REQUIRE(marker.context.count("opponent") == 0);
    REQUIRE(marker.unavailableReason == "table-game:missing=opponent");
    // A complete source has no reason.
    source.facts = {{"rate", "200"}};
    widgets.clear();
    CollectTableGame(source, widgets);
    REQUIRE(widgets.back().unavailableReason.empty());
    REQUIRE(widgets.back().node == "ANDROID_TABLE_GAME");
}
