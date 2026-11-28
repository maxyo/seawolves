#pragma once
// The treasurer's purchase list (goodstransfer, PROGRAM/interface/GoodsTransfer.c) draws its goods
// as raw strings of a string collection over a scrolled picture and opens a good's amount window
// from the mouse position over that picture. This exports the list as rows and lets the UI open
// a good's window the way the script expects:
//   choice ANDROID_GOODS_ORDER  one row per good: label = the good's name (the collection's own
//          text), rowId = the good id, images[0] = the atlas cell GOODS/<id>; actions ["order"]
//          while the list window is active.
// The collection's own GOODS_NAMES texts are dropped: the rows carry them. Everything else the touch
// interface reads from the game's scripts itself (the orders, the ship, the open window).
// An order of `<good id>=<amount>` sets a good's standing order the way a player does
// it: the engine places the interface's mouse position over the good's icon (GoodsIconClick),
// raises the script's ShowItemsWindow, enters the amount in TG_EDIT and presses TG_OK_BUTTON.
#include "mobile_widget.hpp"
#include <algorithm>
#include <optional>

namespace storm::mobile
{

struct GoodsTransferGood
{
    std::string id, name;
};

// The goods in the script's order; listActive: the list window takes input (no amount window).
inline void CollectGoodsTransfer(const std::vector<GoodsTransferGood> &goods, bool listActive,
                                 std::vector<MobileWidget> &widgets)
{
    widgets.erase(
        std::remove_if(widgets.begin(), widgets.end(), [](const MobileWidget &w) { return w.node == "GOODS_NAMES"; }),
        widgets.end());
    int index = 0;
    for (const auto &good : goods)
    {
        MobileWidget row{"ANDROID_GOODS_ORDER", "choice", good.name, "", index, 1000 + index, listActive};
        row.rowId = good.id;
        ImageRef image;
        image.source = {"atlas", "GOODS", good.id, {}, {}, -1};
        row.images.push_back(image);
        if (listActive)
            row.actions.push_back("order");
        widgets.push_back(std::move(row));
        ++index;
    }
}

// The good and amount of an order action's value `<id>=<amount>`.
struct GoodsOrder
{
    std::string good;
    int amount = 0;
};
inline std::optional<GoodsOrder> GoodsTransferOrder(const std::string &value)
{
    const size_t equals = value.rfind('=');
    if (equals == std::string::npos || equals == 0 || equals + 1 == value.size())
        return std::nullopt;
    const std::string digits = value.substr(equals + 1);
    if (digits.size() > 6 || digits.find_first_not_of("0123456789") != std::string::npos)
        return std::nullopt;
    return GoodsOrder{value.substr(0, equals), std::stoi(digits)};
}

// GoodsTransfer.c ShowItemsWindow maps the mouse position (mx, my) to the picture as
// and opens the good whose icon centre (gx, gy) satisfies
//   gx - 32 + wx <= x <= gx + 32 + wx  and  gy - 80 <= y <= gy - 16
// (wx: GetXYWindowOffset, the horizontal centring of the 800-wide layout). The point aims at the
// middle of that box.
struct GoodsIconPicture
{
    float offsetX = 0, offsetY = 0, scaleX = 1, scaleY = 1, windowX = 0;
};
struct GoodsClickPoint
{
    float x = 0, y = 0;
};
inline GoodsClickPoint GoodsIconClick(float gx, float gy, const GoodsIconPicture &p)
{
    return {(gx + p.windowX - p.offsetX) / p.scaleX + 1.0f, (gy - 48.0f - p.offsetY) / p.scaleY + 45.0f};
}
inline bool GoodsIconHit(float mx, float my, float gx, float gy, const GoodsIconPicture &p)
{
    const float x = p.offsetX + (mx - 1.0f) * p.scaleX, y = p.offsetY + (my - 45.0f) * p.scaleY;
    return x >= gx - 32 + p.windowX && x <= gx + 32 + p.windowX && y >= gy - 80 && y <= gy - 16;
}
// GetXYWindowOffset (interface.c): the layout is 600 high, at least 800 wide, centred.
inline float GoodsWindowOffsetX(float width, float height)
{
    const float screen = std::max(800.0f, width * 600.0f / std::max(1.0f, height));
    return (screen - 800.0f) / 2.0f;
}

} // namespace storm::mobile
