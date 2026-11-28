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
    std::vector<MobileWidget> widgets;
    CollectTableGame(source, widgets);
    // The removed picture and the beta line are not exported.
    REQUIRE(widgets.size() == 3);
    REQUIRE((widgets[0].node == "HeroDice1" && widgets[0].type == "image" && widgets[0].label == "dice_3_1"));
    REQUIRE((widgets[0].images.size() == 1 && widgets[0].images[0].source.group == "DICE"));
    REQUIRE(widgets[0].order == 486 * 8192 + 60);
    REQUIRE((widgets[1].node == "PCard18" && widgets[1].label == "blank"));
    REQUIRE((widgets[2].node == "Money" && widgets[2].type == "text" && widgets[2].label == "7 600"));
}
