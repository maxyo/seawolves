#include "mobile_goods_transfer.hpp"
#include <cmath>

#include <catch2/catch.hpp>
using namespace storm::mobile;
TEST_CASE("mobile_goods_transfer")
{
    std::vector<MobileWidget> widgets{{"EXIT_BTN", "button", "EXIT_BTN", "", -1, 1, true},
                                      {"GOODS_NAMES", "text", "Ball", "", 0, 2, true}};
    GoodsTransferSource source;
    source.goods = {{"Balls", "Ball", 600, 400}, {"Food", "Food", 0, std::nullopt}};
    source.listActive = true;
    source.ship = "Lugger 'Swift'";
    source.shipIndex = 0;
    source.shipCount = 2;
    CollectGoodsTransfer(source, widgets);
    // The collection's texts are replaced by the rows and the ship line.
    REQUIRE((widgets.size() == 4 && widgets[0].node == "EXIT_BTN"));
    const auto &balls = widgets[1];
    REQUIRE(
        (balls.node == "ANDROID_GOODS_ORDER" && balls.type == "choice" && balls.rowId == "Balls" && balls.index == 0));
    REQUIRE((balls.value == "0" && balls.enabled && balls.actions == std::vector<std::string>{"order"}));
    REQUIRE((balls.images.size() == 1 && balls.images[0].source.group == "GOODS" &&
             balls.images[0].source.image == "Balls"));
    REQUIRE((balls.context.at("order") == "600" && balls.context.at("hold") == "400"));
    REQUIRE(widgets[2].context.count("hold") == 0);
    REQUIRE((widgets[3].node == "ANDROID_GOODS_SHIP" && widgets[3].context.at("count") == "2" &&
             widgets[3].unavailableReason.empty()));
    // An open amount window: its row is marked, the list takes no input, a missing fact is named.
    source.open = "Food";
    source.listActive = false;
    source.missing = {"companion"};
    widgets.clear();
    CollectGoodsTransfer(source, widgets);
    REQUIRE((widgets[0].value == "0" && widgets[1].value == "1" && !widgets[1].enabled && widgets[1].actions.empty()));
    REQUIRE(widgets[2].unavailableReason == "goods-transfer:missing=companion");
    // Actions bound to this opening only.
    const auto order = GoodsTransferOrder("order@7:Balls=600", 7);
    REQUIRE((order && order->good == "Balls" && order->amount == 600));
    REQUIRE((!GoodsTransferOrder("order@6:Balls=600", 7) && !GoodsTransferOrder("order@7:=5", 7)));
    REQUIRE((!GoodsTransferOrder("order@7:Balls=", 7) && !GoodsTransferOrder("order@7:Balls=-3", 7) &&
             !GoodsTransferOrder("order@7:Balls", 7)));
    // The click point lands in the script's box of that good and of no neighbour (82 apart
    // vertically, 123 horizontally), also scrolled, scaled and on a wide screen.
    for (const GoodsIconPicture picture :
         {GoodsIconPicture{}, GoodsIconPicture{40, 25, 0.8f, 0.8f, GoodsWindowOffsetX(2400, 1080)}})
        for (const float gx : {90.0f, 213.0f})
            for (const float gy : {95.0f, 177.0f})
            {
                const auto p = GoodsIconClick(gx, gy, picture);
                REQUIRE(GoodsIconHit(p.x, p.y, gx, gy, picture));
                REQUIRE(
                    (!GoodsIconHit(p.x, p.y, gx, gy + 82, picture) && !GoodsIconHit(p.x, p.y, gx + 123, gy, picture)));
            }
    REQUIRE((GoodsWindowOffsetX(800, 600) == 0 && std::fabs(GoodsWindowOffsetX(2400, 1080) - 266.6667f) < 0.01f));
    REQUIRE(widgets.back().node == "ANDROID_GOODS_SHIP");
}
