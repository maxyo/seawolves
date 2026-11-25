#pragma once
#include "mobile_image_bridge.hpp"
#include "mobile_widget.hpp"
#include "shared/interface/messages.h"
#include "string_compare.hpp"
#include "xdefines.h"

#include <string>
#include <string_view>
#include <vector>

// The record slot a save screen's picture node shows (SAVEIMG1..SAVEIMG10); -1 for any other node.
inline int MobileSaveSlot(const char *node)
{
    if (!node)
        return -1;
    for (int slot = 0; slot < 10; ++slot)
        if (std::string(node) == "SAVEIMG" + std::to_string(slot + 1))
            return slot;
    return -1;
}

// What the touch UI gets of a picture node (CXI_PICTURE) on the screen `screen`: a save slot, or the image with its
// source (an atlas entry, a portrait, a texture and the rectangle shown) and, for the tab buttons of the stock screens,
// whether the tab is the active one: the stock tab shows the atlas's TabDeSelected artwork then.
template <class Picture>
void MobilePictureCollect(const Picture &p, std::string_view screen, std::vector<MobileWidget> &widgets)
{
    const int slot = MobileSaveSlot(p.m_nodeName);
    if (slot >= 0)
    {
        // Names and dates remain runtime SAVENOTES entries, joined by the presentation.
        widgets.push_back({p.m_nodeName, "save", "", p.m_bMakeBlind ? "1" : "0", slot,
                           p.m_rect.top * 8192 + p.m_rect.left, p.m_bSelected && p.m_bClickable && !p.m_bLockStatus});
        return;
    }
    if (p.m_mobileTexturePath.empty() || p.m_pTex)
        return;
    const auto &v = p.m_v;
    auto ref = storm::mobile::Reference({p.m_mobileTexturePath, {v[0].tu, v[0].tv, v[3].tu, v[3].tv}}, v[0].color);
    if (!ref.handle)
        return;
    if (p.m_pcGroupName && !p.m_mobilePictureName.empty())
        ref.source = {"atlas", p.m_pcGroupName, p.m_mobilePictureName, {}, {}, -1};
    else if (const std::string face = MobilePortraitFace(p.m_mobileTexturePath); !face.empty())
        ref.source = {"portrait", {}, face, {}, {}, -1};
    else if (const std::string texture = MobileTextureName(p.m_mobileTexturePath); !texture.empty())
        ref.source = {"texture", {}, texture, {}, {}, -1, {v[0].tu, v[0].tv, v[3].tu, v[3].tv}};
    MobileWidget widget{p.m_nodeName, "image", "", "", -1, p.m_rect.top * 8192 + p.m_rect.left, false};
    const std::string name = p.m_nodeName ? p.m_nodeName : "";
    const bool numberedTab = name.size() == 8 && name.compare(0, 7, "TABBTN_") == 0 && name[7] >= '1' && name[7] <= '4';
    const bool journalTab =
        screen == "questbook" &&
        (name == "TABBTN_QUEST" || name == "TABBTN_QUESTARJ" || name == "TABBTN_INFO" || name == "TABBTN_CASHBOOK" ||
         name == "TABBTN_STATISTIC" || name == "TABBTN_SHIP_PLACE" || name == "TABBTN_STOREBOOK");
    if ((numberedTab || journalTab) && p.m_pcGroupName && storm::iEquals(p.m_pcGroupName, "TABS"))
    {
        FXYRECT active;
        if (p.pPictureService->GetTexturePos(p.m_pcGroupName, "TabDeSelected", active))
            widget.value =
                v[0].tu == active.left && v[0].tv == active.top && v[3].tu == active.right && v[3].tv == active.bottom
                    ? "1"
                    : "0";
    }
    widget.enabled = p.m_bSelected && p.m_bClickable && !p.m_bLockStatus;
    if (widget.enabled && (p.CheckCommandUsed(ACTION_MOUSECLICK) || p.CheckCommandUsed(ACTION_ACTIVATE)))
        widget.actions.push_back("press");
    widget.images.push_back(ref);
    widgets.push_back(std::move(widget));
}
