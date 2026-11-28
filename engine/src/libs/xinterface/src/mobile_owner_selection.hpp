#pragma once
#include "mobile_inventory.hpp"
#include "mobile_officer_selection.hpp"

namespace storm::mobile
{
inline const char *OwnerField(std::string_view screen, std::string_view node, bool vertical)
{
    if (vertical)
        return node == "SHIPS_SCROLL" && (screen == "ship" || screen == "shipyard") ? "character" : nullptr;
    if (node == "SHIPS_SCROLL" && (screen == "store" || screen == "storage" || screen == "contraband" ||
                                   screen == "cannons" || screen == "hirecrew" || screen == "repair"))
        return "companionIndex";
    if (node == "CHARACTERS_SCROLL" && (screen == "itemstrade" || screen == "itemsbox"))
        return "character";
    if (node == "PASSENGERSLIST" && (screen == "shipyard" || screen == "ship" || screen == "character" ||
                                     screen == "lscchar_all" || screen == "transfer_main"))
        return "character";
    return nullptr;
}

// Why a game's owner list with rows exports none for its `current` (missing or past the list), "" when it is valid
// or the list is none of the owner lists: the screen would show no selector at all.
inline std::string OwnerCurrentDefect(std::string_view screen, std::string_view node, bool vertical, ATTRIBUTES *attrs,
                                      size_t capacity)
{
    if (!OwnerField(screen, node, vertical) || !attrs)
        return {};
    const auto count = InventoryNumber(attrs->GetAttribute("ListSize"));
    if (!count || !*count || *count > 1000 || *count > capacity)
        return {};
    const char *raw = attrs->GetAttribute("current");
    const auto selected = InventoryNumber(raw);
    if (!selected)
        return "its current is '" + std::string(raw ? raw : "") + "', not a row";
    if (*selected >= *count)
        return "its current " + std::to_string(*selected) + " is past its " + std::to_string(*count) + " rows";
    return {};
}

// Resolve names from actual Characters attributes, never from an item index or
// a copied fixture. Missing trailing image slots are stock padding, not owners.
template <class Resolve>
bool OwnerRows(std::string_view screen, std::string_view node, bool vertical, ATTRIBUTES *attrs, size_t capacity,
               uint64_t revision, const char *profile, bool enabled, int order, Resolve resolve,
               const std::string &emptyShip, OfficerListIdentity &state, std::vector<MobileWidget> &rows)
{
    const char *field = OwnerField(screen, node, vertical);
    if (!field || !attrs || !revision || !profile || !*profile)
        return false;
    const auto count = InventoryNumber(attrs->GetAttribute("ListSize"));
    const auto selected = InventoryNumber(attrs->GetAttribute("current"));
    if (!count || !*count || *count > 1000 || *count > capacity || !selected || *selected >= *count)
        return false;
    state = {};
    rows.clear();
    state.revision = revision;
    state.profile = std::string(profile) + "\n" + std::string(screen) + "\n" + std::string(node);
    for (uint32_t i = 0; i < *count; ++i)
    {
        auto *entry = attrs->GetAttributeClass("pic" + std::to_string(uint64_t(i) + 1));
        if (!entry)
        {
            state.rows.push_back("padding");
            continue;
        }
        // character_all.c FillPassengerScroll creates pic(m+1) before checking
        // the passenger's eligibility, then publishes ListSize=m+2. A rejected
        // last passenger leaves this empty penultimate slot and an absent last
        // slot. It is padding, not an invalid owner (nor a selectable candidate).
        if (node == "PASSENGERSLIST" && (screen == "character" || screen == "lscchar_all") &&
            uint64_t(i) + 2 == *count && !entry->HasValue() && entry->GetAttributesNum() == 0 &&
            !attrs->GetAttributeClass("pic" + std::to_string(uint64_t(i) + 2)))
        {
            state.rows.push_back("padding");
            continue;
        }
        const char *raw = entry->GetAttribute(field);
        const bool empty = screen == "shipyard" && node == "SHIPS_SCROLL" && raw && std::string(raw) == "-1";
        auto owner = InventoryNumber(raw);
        if (!empty && !owner)
            return false;
        std::string label = empty ? emptyShip : resolve(*owner, node == "SHIPS_SCROLL");
        if (label.empty())
            return false;
        state.rows.push_back(std::string(raw) + "\n" + label);
        rows.push_back({std::string(node), "choice", label, *selected == i ? "1" : "0", static_cast<int>(i),
                        order + static_cast<int>(i), enabled});
    }
    return !rows.empty();
}
// Stock hirecrew updates refCharacter in ProcessFrame, after scrolling. The
// selector alone is insufficient proof that transactions address that owner.
inline bool HireCrewBoundOwner(ATTRIBUTES *selector, ATTRIBUTES *character)
{
    if (!selector || !character)
        return false;
    const auto selected = InventoryNumber(selector->GetAttribute("current"));
    const auto count = InventoryNumber(selector->GetAttribute("ListSize"));
    const auto actual = InventoryNumber(character->GetAttribute("index"));
    if (!selected || !count || *selected >= *count || !actual)
        return false;
    auto *row = selector->GetAttributeClass("pic" + std::to_string(uint64_t(*selected) + 1));
    const auto owner = InventoryNumber(row ? row->GetAttribute("companionIndex") : nullptr);
    return owner && *owner == *actual;
}

inline bool HireCrewQuantityNode(std::string_view node)
{
    return node == "QTY_EDIT" || node == "qty_edit" || node == "QTY_OK_BUTTON" || node == "QTY_CANCEL_BUTTON" ||
           node == "QTY_ADD_BUTTON" || node == "QTY_REMOVE_BUTTON" || node == "QTY_ADD_ALL_BUTTON" ||
           node == "QTY_REMOVE_ALL_BUTTON";
}

// A quantity action guarded by `guard`, the owner row it was made for, validated against the last published owner
// capability without selecting a ship or dispatching a script event: a queued edit must not target a new owner.
inline bool HireCrewQuantityGuard(std::string_view screen, std::string_view node, const OfficerSelection &published,
                                  const OfficerListIdentity &state, const std::vector<MobileWidget> &rows,
                                  const std::string &guard)
{
    if (screen != "hirecrew" || !HireCrewQuantityNode(node))
        return false;
    const MobileWidget *selected = nullptr;
    for (const auto &row : rows)
        if (row.value == "1")
        {
            if (selected || !row.enabled || row.node != "SHIPS_SCROLL")
                return false;
            selected = &row;
        }
    return selected && published.accepts(state, selected->index, guard);
}

} // namespace storm::mobile
