#include "mobile_goods_transfer.hpp"
#include <cmath>

#include <catch2/catch.hpp>
using namespace storm::mobile;
TEST_CASE("mobile_goods_transfer")
{
    std::vector<MobileWidget> widgets{{"EXIT_BTN", "button", "EXIT_BTN", "", -1, 1, true},
                                      {"GOODS_NAMES", "text", "Ball", "", 0, 2, true}};
    const std::vector<GoodsTransferGood> goods{{"Balls", "Ball"}, {"Food", "Food"}};
    CollectGoodsTransfer(goods, true, widgets);
    // The collection's texts are replaced by the rows.
    REQUIRE((widgets.size() == 3 && widgets[0].node == "EXIT_BTN"));
    const auto &balls = widgets[1];
    REQUIRE(
        (balls.node == "ANDROID_GOODS_ORDER" && balls.type == "choice" && balls.rowId == "Balls" && balls.index == 0));
    REQUIRE((balls.label == "Ball" && balls.enabled && balls.actions == std::vector<std::string>{"order"}));
    REQUIRE((balls.images.size() == 1 && balls.images[0].source.group == "GOODS" &&
             balls.images[0].source.image == "Balls"));
    // An open amount window: the list takes no order.
    widgets.clear();
    CollectGoodsTransfer(goods, false, widgets);
    REQUIRE((!widgets[1].enabled && widgets[1].actions.empty()));
    const auto order = GoodsTransferOrder("Balls=600");
    REQUIRE((order && order->good == "Balls" && order->amount == 600));
    REQUIRE((!GoodsTransferOrder("=5") && !GoodsTransferOrder("Balls=") && !GoodsTransferOrder("Balls=-3") &&
             !GoodsTransferOrder("Balls")));
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
}
