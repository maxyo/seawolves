#pragma once
#include "core.h"
#include "mobile_image_bridge.hpp"
#include "mobile_inventory.hpp"
#include "mobile_widget.hpp"
#include "storm_ui/action.h"
#include "shared/interface/messages.h"
#include "xdefines.h"

#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// What the touch UI gets of a table node (CXI_TABLE) on the screen `screen`: one "choice" widget per row with drawable
// content, its cells and the header row; or one "table" widget when no row has content.
template <class Table>
void MobileTableCollect(const Table &t, std::string_view screen, std::vector<MobileWidget> &widgets)
{
    auto *root = core.Entity_GetAttributeClass(g_idInterface, t.m_nodeName);
    if (!root)
        return;
    // Headers and body use the same safe source references and opaque image handles.
    const auto collectCell = [&](ATTRIBUTES *cell, int column) {
        MobileCell output;
        output.column = column;
        output.colorArgb = t.m_dwFontCellColor;
        if (!cell)
            return output;
        if (const char *text = cell->GetAttribute("str"))
            output.text = text;
        output.colorArgb = cell->GetAttributeAsDword("color", t.m_dwFontCellColor);
        const auto addIcon = [&](ATTRIBUTES *icon) {
            if (!icon)
                return;
            storm::mobile::ImageRef ref;
            if (icon->GetAttribute("group"))
                ref = storm::mobile::AtlasReference(t.pPictureService, icon->GetAttribute("group"),
                                                    icon->GetAttribute("image"));
            else if (const char *texture = icon->GetAttribute("texture"))
            {
                storm::mobile::Crop uv;
                if (const char *text = icon->GetAttribute("uv"))
                    if (sscanf(text, "%f,%f,%f,%f", &uv.left, &uv.top, &uv.right, &uv.bottom) != 4)
                        return;
                ref = storm::mobile::Reference({texture, uv});
            }
            if (ref.handle)
                output.images.push_back(ref);
        };
        if (auto *icon = cell->GetAttributeClass("icon"))
            addIcon(icon);
        else
            for (int n = 1; n <= 8; ++n)
            {
                auto name = "icon" + std::to_string(n);
                auto *icon = cell->GetAttributeClass(name.c_str());
                if (!icon)
                    break;
                addIcon(icon);
            }
        return output;
    };
    std::vector<std::string> columnHeaders;
    std::optional<std::vector<MobileCell>> headerCells;
    if (auto *header = root->GetAttributeClass("hr"))
    {
        headerCells.emplace();
        for (int col = 0; col < t.m_nColQuantity; ++col)
        {
            const std::string key = "td" + std::to_string(col + 1);
            auto *cell = header->GetAttributeClass(key.c_str());
            const char *text = cell ? cell->GetAttribute("str") : nullptr;
            columnHeaders.emplace_back(text ? text : "");
            if (!cell)
                continue;
            auto output = collectCell(cell, col);
            // Only a single actual nation image supplies a caption; never infer from its column.
            if (output.text.empty() && output.images.size() == 1 && t.pStringService)
            {
                const auto &source = output.images.front().source;
                if (source.kind == "atlas" && source.group == "NATIONS" && !source.image.empty())
                    if (const char *caption = t.pStringService->GetString(source.image.c_str()); caption && *caption)
                        output.text = caption;
            }
            headerCells->push_back(std::move(output));
        }
    }
    auto *interfaceRoot = core.Entity_GetAttributeClass(g_idInterface, "");
    const size_t before = widgets.size();
    for (int row = 0; row < t.m_nLineQuantity; ++row)
    {
        const std::string key = "tr" + std::to_string(row + 1);
        auto *line = root->GetAttributeClass(key.c_str());
        if (!line)
            break;
        MobileWidget widget{t.m_nodeName,
                            "choice",
                            "",
                            row == t.m_nSelectIndex ? "1" : "0",
                            row,
                            t.m_rect.top * 8192 + t.m_rect.left + row,
                            t.m_bClickable && !t.m_bLockStatus};
        widget.rowId = storm::mobile::InterfaceTableRowId(screen, t.m_nodeName, line, interfaceRoot);
        if (storm::mobile::LeasedTableIdentityRequired(screen, t.m_nodeName) && widget.rowId.empty())
            widget.enabled = false;
        widget.columnHeaders = columnHeaders;
        widget.headerCells = headerCells;
        if (widget.enabled)
        {
            widget.actions.push_back("select");
            if (t.CheckCommandUsed(ACTION_ACTIVATE) || t.CheckCommandUsed(ACTION_MOUSEDBLCLICK))
                widget.actions.push_back("activate");
        }
        bool hasContent = false;
        for (int col = 0; col < t.m_nColQuantity; ++col)
        {
            if (col)
                widget.label += '\t';
            const std::string cellKey = "td" + std::to_string(col + 1);
            auto *cell = line->GetAttributeClass(cellKey.c_str());
            auto output = collectCell(cell, col);
            widget.label += output.text;
            hasContent = hasContent || !output.text.empty() || !output.images.empty();
            widget.cells.push_back(std::move(output));
        }
        if (hasContent)
            widgets.push_back(std::move(widget));
    }
    // Rows are the table's widgets, so a table without rows would vanish from the model and
    // look like an interface without that table. One "table" widget (index -1, the headers,
    // no verbs) says the table is there and empty (an unvisited trade book, an empty chest).
    if (widgets.size() == before)
    {
        MobileWidget empty{t.m_nodeName, "table", "", "0", -1, t.m_rect.top * 8192 + t.m_rect.left, false};
        empty.columnHeaders = columnHeaders;
        empty.headerCells = headerCells;
        widgets.push_back(std::move(empty));
    }
}

// A touch action on row `index` of a table node: "select", "activate" or "dispatch" with the row's identity. An
// activation is queued in `actions` as a dispatch, applied after the selection callback has run. A refusal says which
// condition failed in t.mobileRefusal_ (logged by XINTERFACE).
template <class Table, class Actions> bool MobileTableApply(Table &t, const storm::ui::Action &action, Actions &actions)
{
    const int index = action.index;
    const std::string_view screen = action.screen;
    t.mobileRefusal_.clear();
    const auto refuse = [&t](std::string why) {
        t.mobileRefusal_ = std::move(why);
        return false;
    };
    if (index < 0 || index >= t.m_nLineQuantity)
        return refuse("row " + std::to_string(index) + " is outside the table's " + std::to_string(t.m_nLineQuantity) +
                      " rows");
    if (!t.m_bUse || !t.m_bClickable || t.m_bLockedNode || t.m_bLockStatus)
        return refuse(std::string("the table is ") + (!t.m_bUse         ? "not in use"
                                                      : !t.m_bClickable ? "not clickable"
                                                      : t.m_bLockedNode ? "locked by a window"
                                                                        : "lock-status"));
    auto *root = core.Entity_GetAttributeClass(g_idInterface, t.m_nodeName);
    const std::string key = "tr" + std::to_string(index + 1);
    auto *row = root ? root->GetAttributeClass(key.c_str()) : nullptr;
    if (!row)
        return refuse("the table has no " + key);
    const auto id =
        storm::mobile::InterfaceTableRowId(screen, t.m_nodeName, row, core.Entity_GetAttributeClass(g_idInterface, ""));
    const bool leased = storm::mobile::LeasedTableIdentityRequired(screen, t.m_nodeName);
    if (leased && id.empty())
        return refuse(key + " has no identity");
    const bool same = action.value == id || (!leased && action.value.empty() && action.verb == "select");
    const bool select = same && action.verb == "select";
    const bool activate = same && action.verb == "activate";
    const bool dispatch = same && action.verb == "dispatch";
    if (!select && !activate && !dispatch)
        return refuse("'" + action.verb + " " + action.value + "' is not for " + key + " now (" + id + ")");
    if (activate || dispatch)
    {
        const int code = t.CheckCommandUsed(ACTION_ACTIVATE) ? ACTION_ACTIVATE : ACTION_MOUSEDBLCLICK;
        if (!t.CheckCommandUsed(code))
            return refuse("the table has no activate command");
        if (dispatch)
        {
            if (t.m_nSelectIndex != index)
                return refuse("row " + std::to_string(t.m_nSelectIndex) + " is selected, not " + std::to_string(index));
            bool stop = false;
            t.DoAction(code, stop, true);
            return true;
        }
        // Re-resolve node, screen epoch and row identity in XINTERFACE after the selection
        // callback. That callback may rebuild the table or destroy this node entirely.
        storm::ui::Action next = action;
        next.verb = "dispatch";
        next.request = 0;
        actions.push_front(std::move(next));
    }
    // A mouse selection focuses the table before TableSelectChange, and scripts depend on
    // it: shipyard's ProcessFrame resets the chosen offer while SHIPS_SCROLL is current,
    // transfer_main's hides the cargo arrows (B_LEFT / B_RIGHT) unless TABLE_LIST is.
    core.Send_Message(g_idInterface, "ls", MSG_INTERFACE_NEW_CURRENT_NODE, t.m_nodeName);
    t.SetTopIndexForSelect(index);
    t.SelectLine(index);
    return true;
}
