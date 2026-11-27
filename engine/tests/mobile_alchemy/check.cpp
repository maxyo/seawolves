#include "mobile_alchemy.hpp"

#include <catch2/catch.hpp>
using namespace storm::mobile;
TEST_CASE("mobile_alchemy")
{
    AlchemySource source;
    source.recipes = {{"cartridge", "Cartridge", "ITEMS_18", "itm8", 0}, {"potion1", "Potion", "ITEMS_1", "itm2", 1}};
    source.current = "cartridge";
    std::vector<MobileWidget> widgets;
    CollectAlchemy(source, widgets);
    // The shown recipe marked; the other can be chosen.
    REQUIRE((widgets.size() == 2 && widgets[0].node == "ANDROID_ALCHEMY_RECIPE" && widgets[0].value == "1" &&
             widgets[0].actions.empty()));
    REQUIRE((widgets[1].rowId == "potion1" && widgets[1].index == 1 &&
             widgets[1].actions == std::vector<std::string>{"choose"}));
    REQUIRE((widgets[0].images[0].source.group == "ITEMS_18" && widgets[0].images[0].source.image == "itm8"));
    // Busy or missing facts: nothing to choose.
    source.busy = "scrolling";
    widgets.clear();
    CollectAlchemy(source, widgets);
    REQUIRE(widgets[1].actions.empty());
}
