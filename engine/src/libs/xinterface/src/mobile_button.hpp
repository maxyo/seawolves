#pragma once
#include "mobile_widget.hpp"
#include "shared/interface/messages.h"

#include <string>
#include <string_view>
#include <vector>

// What the touch UI gets of a button node (CXI_BUTTON) on the screen `screen`: a button with its text, or, for the
// icon buttons of the screens ported to the portable UI, with a label the UI words itself.
template <class Button>
void MobileButtonCollect(const Button &b, std::string_view screen, std::vector<MobileWidget> &widgets)
{
    const std::string name = b.m_nodeName ? b.m_nodeName : "";
    // This icon has a real ExitMapWindow script command but no text resource.
    // Export only the live close capability; Android supplies its localized label.
    if (name == "MAP_EXIT_BTN" && screen == "items")
    {
        if (b.m_bUse && b.m_bClickable && !b.m_bLockedNode && !b.m_bLockStatus &&
            (b.CheckCommandUsed(ACTION_MOUSECLICK) || b.CheckCommandUsed(ACTION_ACTIVATE)))
        {
            MobileWidget widget{name, "button", "×", "", -1, b.m_rect.top * 8192 + b.m_rect.left, true};
            widget.actions.push_back("press");
            widgets.push_back(std::move(widget));
        }
        return;
    }
    const char *label = nullptr;
    if (name.compare(0, 11, "LEFTCHANGE_") == 0)
        label = "‹";
    else if (name.compare(0, 12, "RIGHTCHANGE_") == 0)
        label = "›";
    else if (b.m_idString != -1)
        label = b.pStringService->GetString(b.m_idString);
    // Stock commerce quantity arrows are command-bearing icon buttons without
    // a string resource. Export only these known controls, never arbitrary IDs.
    if ((!label || !*label) &&
        (screen == "store" || screen == "storage" || screen == "contraband" || screen == "cannons" ||
         screen == "itemstrade" || screen == "itemsbox" || screen == "hirecrew"))
    {
        if (name == "QTY_ADD_BUTTON")
            label = "+";
        else if (name == "QTY_REMOVE_BUTTON")
            label = "−";
        else if (name == "QTY_ADD_ALL_BUTTON")
            label = "+ max";
        else if (name == "QTY_REMOVE_ALL_BUTTON")
            label = "− max";
        if (label && !b.m_bUse)
            return;
    }
    // Screens ported to the portable UI that work through icon buttons without text: the
    // tavern mini-games (cup, deck, dice, pass), repair (amount arrows), the loot / exchange
    // screen (ship and crew arrows, quantity arrows), sail emblems, purchase list,
    // alchemy (scroll and quantity arrows) and the ship screen (guns per side arrows of its
    // cannons window). Export them by node name; the UI draws its own words for them.
    static constexpr const char *iconScreens[] = {"boal_dice",   "boal_cards",    "repair",  "transfer_main",
                                                  "sailsgerald", "goodstransfer", "alchemy", "ship"};
    for (const char *id : iconScreens)
        if ((!label || !*label) && screen == id)
            label = b.m_nodeName;
    // These icon/hit-area nodes deliberately keep text in STR_BUTTONS. Export
    // the actual command-bearing node, using the same localized resource key.
    if ((!label || !*label) && b.pStringService)
    {
        std::string baseName = name;
        if (baseName.size() > 2 && baseName.compare(baseName.size() - 2, 2, "_2") == 0)
            baseName.resize(baseName.size() - 2);
        static constexpr struct
        {
            const char *node;
            const char *key;
        } navigation[] = {{"I_SHIP", "buttonShip"},      {"I_QUESTBOOK", "buttonQuestBook"},
                          {"I_NATIONS", "buttonNation"}, {"I_TRADEBOOK", "buttonTradeBook"},
                          {"I_ITEMS", "Sel Items"},      {"I_CHARACTER", "buttonCharacter"}};
        for (const auto &entry : navigation)
            if (baseName == entry.node)
            {
                const auto id = b.pStringService->GetStringNum(entry.key);
                if (id != -1)
                    label = b.pStringService->GetString(id);
                break;
            }
    }
    if (label && *label)
        widgets.push_back({name, "button", label, "", -1, b.m_rect.top * 8192 + b.m_rect.left,
                           b.m_bUse && b.m_bClickable && !b.m_bLockedNode && !b.m_bLockStatus &&
                               (b.CheckCommandUsed(ACTION_MOUSECLICK) || b.CheckCommandUsed(ACTION_ACTIVATE))});
}
