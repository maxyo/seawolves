// What the touch UI gets of a button node (MobileButtonCollect, mobile_button.hpp): the items screen's map close
// button only while its script command is live, the stock navigation buttons by their string resource, and the
// commerce screens' quantity arrows, a button drawn as the arrow picture.
#include "mobile_button.hpp"

#include <cassert>
#include <iostream>

// The interface's strings: "Ship" under buttonShip.
struct Strings
{
    const char *GetString(int) const
    {
        return "Ship";
    }
    int GetStringNum(const char *key) const
    {
        return std::string(key) == "buttonShip" ? 0 : -1;
    }
};
// The members of CXI_BUTTON that MobileButtonCollect reads.
struct Button
{
    const char *m_nodeName = "MAP_EXIT_BTN";
    int m_idString = -1;
    int m_arrow = 0;
    const Strings *pStringService = nullptr;
    bool m_bUse = true, m_bClickable = true, m_bLockedNode = false, m_bLockStatus = false;
    bool click = true, activate = false;
    struct
    {
        int top = 20, left = 731;
    } m_rect;
    bool CheckCommandUsed(int command) const
    {
        return command == ACTION_MOUSECLICK ? click : activate;
    }
};

int main()
{
    Button button;
    std::string screen = "items";
    const auto collect = [&] {
        std::vector<MobileWidget> widgets;
        MobileButtonCollect(button, screen, widgets);
        return widgets;
    };
    auto widgets = collect();
    assert(widgets.size() == 1 && widgets[0].node == "MAP_EXIT_BTN" && widgets[0].label == "×");
    assert(widgets[0].type == "button" && widgets[0].index == -1 && widgets[0].enabled);
    assert(widgets[0].actions == std::vector<std::string>{"press"});
    for (bool *flag : {&button.m_bUse, &button.m_bClickable})
    {
        *flag = false;
        assert(collect().empty());
        *flag = true;
    }
    for (bool *flag : {&button.m_bLockedNode, &button.m_bLockStatus})
    {
        *flag = true;
        assert(collect().empty());
        *flag = false;
    }
    button.click = false;
    assert(collect().empty());
    button.activate = true;
    assert(collect().size() == 1);
    screen = "questbook";
    assert(collect().empty());
    screen = "items";
    button.m_nodeName = "UNLABELED_OTHER";
    assert(collect().empty());
    Strings strings;
    button.pStringService = &strings;
    for (const char *node : {"I_SHIP", "I_SHIP_2"})
    {
        button.m_nodeName = node;
        widgets = collect();
        assert(widgets.size() == 1 && widgets[0].label == "Ship" && widgets[0].actions.empty());
    }
    button.m_nodeName = "I_QUESTBOOK"; // no such string: not shown
    assert(collect().empty());
    button.pStringService = nullptr;
    for (const char *commerce : {"store", "hirecrew"})
    {
        screen = commerce;
        for (const char *name : {"QTY_ADD_BUTTON", "QTY_REMOVE_BUTTON", "QTY_ADD_ALL_BUTTON", "QTY_REMOVE_ALL_BUTTON"})
        {
            button.m_nodeName = name;
            widgets = collect();
            assert(widgets.size() == 1 && widgets[0].enabled && !widgets[0].label.empty());
            button.m_bUse = false;
            assert(collect().empty());
            button.m_bUse = true;
            button.m_bLockedNode = true;
            assert(!collect()[0].enabled);
            button.m_bLockedNode = false;
            button.click = button.activate = false;
            assert(!collect()[0].enabled);
            button.click = true;
        }
    }
    screen = "save_load";
    assert(collect().empty());
    screen = "store";
    button.m_nodeName = "QTY_UNKNOWN_BUTTON";
    assert(collect().empty());
    screen = "alchemy"; // an icon screen: the node's name, the UI words it
    widgets = collect();
    assert(widgets.size() == 1 && widgets[0].label == "QTY_UNKNOWN_BUTTON");
    screen = "selectcharacter"; // drawn as the interface's arrows: ‹ › steps on any screen, whatever the name
    button.m_nodeName = "BUTTON_L_TRAIT";
    assert(collect().empty());
    button.m_arrow = -1;
    widgets = collect();
    assert(widgets.size() == 1 && widgets[0].label == "‹" && widgets[0].enabled);
    button.m_arrow = 1;
    assert(collect()[0].label == "›");
    std::cout << "PASS map close hidden/disabled/screen/command boundaries, legacy navigation, quantity arrows, arrow "
                 "pictures\n";
}
