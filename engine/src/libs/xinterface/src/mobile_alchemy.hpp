#pragma once
// Alchemy (PROGRAM/interface/alchemy.c) keeps its recipes in a scroll of pictures. This exports them as rows the
// touch interface's script lists, and choosing one the way a tap on the scroll does:
//   choice ANDROID_ALCHEMY_RECIPE  one row per recipe (the scroll's picN records with an item;
//          ListSize/NotUsed padding is never a row): label = the item's name, rowId = item id,
//          value "1" = the recipe shown (sCurItem), index = its scroll position, images[0] = the item's atlas
//          picture; actions ["choose"] while the screen is ready and the row is not the shown recipe.
// `choose@<screen revision>:<item>` selects that recipe in the scroll. The batches, their making and their time are
// the game's (the script's adapter asks them of the game's functions).
#include "mobile_widget.hpp"
#include <string>
#include <vector>

namespace storm::mobile
{

struct AlchemyRecipe
{
    std::string id, name, atlasGroup, atlasImage;
    int index = 0; // scroll position (picN → N − 1)
};

struct AlchemySource
{
    std::vector<AlchemyRecipe> recipes;
    std::string current;              // the recipe shown (sCurItem)
    std::string busy;                 // why the screen is not ready now, "" = ready
    std::vector<std::string> missing; // what the script lacks: reported, and nothing can be chosen
};

inline void CollectAlchemy(const AlchemySource &source, std::vector<MobileWidget> &widgets)
{
    const bool ready = source.busy.empty() && source.missing.empty();
    int order = 0;
    for (const auto &recipe : source.recipes)
    {
        const bool shown = recipe.id == source.current;
        MobileWidget row{
            "ANDROID_ALCHEMY_RECIPE", "choice", recipe.name, shown ? "1" : "0", recipe.index, 100 + order++, true};
        row.rowId = recipe.id;
        ImageRef image;
        image.source = {"atlas", recipe.atlasGroup, recipe.atlasImage, {}, {}, -1};
        row.images.push_back(image);
        if (ready && !shown)
            row.actions.push_back("choose");
        widgets.push_back(std::move(row));
    }
}

// `choose@<revision>:<item>` of this screen opening: the item, empty when the value is not that.
inline std::string ParseAlchemyChoice(const std::string &value, uint64_t revision)
{
    const std::string prefix = "choose@" + std::to_string(revision) + ":";
    return value.rfind(prefix, 0) == 0 ? value.substr(prefix.size()) : std::string();
}

} // namespace storm::mobile
