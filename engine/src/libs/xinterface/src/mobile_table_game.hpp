#pragma once
// The tavern mini-games (boal_dice, boal_cards) draw their dice, cards, coins and money lines
// with script images (CreateImage) and script strings (CreateString), not with nodes. This
// exports them as widgets (the round's state the touch interface reads from the scripts itself):
//   image  <image name>  label = picture, images[0] = the atlas cell (only images with a picture)
//   text   <string name> label = the text (GameInterface.strings; the beta-test lines are left out)
#include "mobile_widget.hpp"

namespace storm::mobile
{

struct TableGameImage
{
    std::string node;
    int x = 0, y = 0;
    ImageRef image; // source.group / source.image name the picture
};

struct TableGameSource
{
    std::vector<TableGameImage> images;
    std::vector<std::pair<std::string, std::string>> strings;
};

inline void CollectTableGame(const TableGameSource &source, std::vector<MobileWidget> &widgets)
{
    for (const auto &image : source.images)
    {
        if (image.image.source.group.empty() || image.image.source.image.empty())
            continue; // CreateImage(name, "", "") removes a picture
        MobileWidget widget{image.node, "image", image.image.source.image, "", -1, image.y * 8192 + image.x, false};
        widget.images.push_back(image.image);
        widgets.push_back(std::move(widget));
    }
    for (const auto &[name, text] : source.strings)
    {
        if (name.rfind("Beta_", 0) == 0)
            continue;
        widgets.push_back({name, "text", text, "", -1, 0, false});
    }
}

} // namespace storm::mobile
