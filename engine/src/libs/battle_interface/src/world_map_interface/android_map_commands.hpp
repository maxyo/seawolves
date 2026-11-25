#pragma once
#ifdef __ANDROID__
#include "bi_defines.h"
#include "platform/android_ui.hpp"
#include "platform/android_worldmap.hpp"
#include "shared/battle_interface/msg_control.h"

#include <optional>
#include <string>
#include <vector>

namespace storm::android
{
// The world map's commands as the touch HUD shows them, from the rows (sCommandName, sNote) of the stock command list;
// Cancel is the HUD's own. None while a row is incomplete, and none (reported) when the HUD has no place for one.
template <class Rows> std::vector<MapCommand> mapCommands(const Rows &rows)
{
    std::vector<MapCommand> out;
    for (const auto &row : rows)
    {
        if (row.sCommandName == "Cancel" || row.sCommandName == "cancel")
            continue;
        if (row.sCommandName.empty() || row.sNote.empty())
            return {};
        if (!knownMapCommand(row.sCommandName))
        {
            logChannelDefect("worldMap", "the HUD has no place for the command " + row.sCommandName);
            return {};
        }
        out.push_back({row.sCommandName, scriptCaption(row.sNote, "worldMap")});
    }
    return out;
}
// The stock list's row of a command; nullopt when it is not offered.
template <class Rows> std::optional<size_t> mapCommandRow(const Rows &rows, const std::string &id)
{
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].sCommandName == id)
            return i;
    return std::nullopt;
}
// A tap on a map command (WM_INTERFACE): refused while the map is hidden or its own command menu is open. The stock
// list is rebuilt, the command selected in it (refused when not offered) and run by the stock activation (its
// end-check, launch event, sound and teardown); a command that leaves for the open sea first marks the stop as the
// player's own.
template <class Map> bool activateMapCommand(Map &map, const std::string &id)
{
    if (!map.m_bVisible || !map.m_pCommandList || map.IsCommandMenuActive())
        return false;
    map.m_nCommandMode = BI_COMMODE_COMMAND_SELECT;
    map.UpdateCommandList();
    if (!map.m_pCommandList->SelectAndroidMapCommand(id))
        return false;
    if (leavesForOpenSea(id))
        map.MarkStopOnPurpose();
    map.ExecuteCommand(BI_MSG_COMMAND_ACTIVATE);
    return true;
}
} // namespace storm::android
#endif
