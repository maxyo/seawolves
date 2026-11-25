// What the touch UI gets of a picture node (MobilePictureCollect, mobile_picture.hpp): the active tab of the stock
// screens told by the atlas crop shown, the image's source, the lock, and the save slots.
#include "mobile_picture.hpp"

#include <cassert>
#include <iostream>

// The interface's picture service: the TABS atlas's TabDeSelected artwork.
struct Pictures
{
    bool found = true;
    bool GetTexturePos(const char *group, const char *name, FXYRECT &out) const
    {
        assert(std::string(group) == "TABS" && std::string(name) == "TabDeSelected");
        out = {0, 0, .5f, .5f};
        return found;
    }
} pictures;

// The members of CXI_PICTURE that MobilePictureCollect reads.
struct Picture
{
    const char *m_nodeName = "TABBTN_QUEST";
    const char *m_pcGroupName = nullptr;
    std::string m_mobileTexturePath = "resource/textures/interfaces/tabs.tga.tx";
    std::string m_mobilePictureName;
    void *m_pTex = nullptr;
    struct Vertex
    {
        float tu = 0, tv = 0;
        uint32_t color = 0xffffffff;
    } m_v[4];
    struct
    {
        int left = 0, top = 0;
    } m_rect;
    bool m_bSelected = true, m_bClickable = true, m_bLockStatus = false, m_bMakeBlind = false;
    bool commands = true;
    const Pictures *pPictureService = &pictures;
    bool CheckCommandUsed(int) const
    {
        return commands;
    }
};

int main()
{
    Picture picture;
    picture.m_pcGroupName = "TABS";
    picture.m_v[3].tu = picture.m_v[3].tv = .5f;
    std::string screen = "questbook";
    const auto read = [&] {
        std::vector<MobileWidget> widgets;
        MobilePictureCollect(picture, screen, widgets);
        assert(widgets.size() == 1);
        return widgets.front();
    };
    for (const char *node : {"TABBTN_QUEST", "TABBTN_QUESTARJ", "TABBTN_INFO", "TABBTN_CASHBOOK", "TABBTN_STATISTIC",
                             "TABBTN_SHIP_PLACE", "TABBTN_STOREBOOK", "TABBTN_1", "TABBTN_4"})
    {
        picture.m_nodeName = node;
        assert(read().value == "1");
        picture.m_v[3].tv = 1;
        assert(read().value == "0");
        picture.m_v[3].tv = .5f;
        pictures.found = false;
        assert(read().value.empty());
        pictures.found = true;
    }
    picture.m_nodeName = "TABBTN_QUEST";
    screen = "items"; // the journal's tabs only on the journal
    assert(read().value.empty());
    picture.m_nodeName = "TABBTN_1";
    assert(read().value == "1");
    screen = "questbook";
    for (const char *node : {"TABBTN_QUEST_UNKNOWN", "TABBTN_5", "TABBTN_QUESTARJ2", "ICON"})
    {
        picture.m_nodeName = node;
        assert(read().value.empty());
    }
    picture.m_nodeName = "TABBTN_QUEST";
    picture.m_pcGroupName = "OTHER";
    assert(read().value.empty());
    picture.m_pcGroupName = nullptr;
    assert(read().value.empty());
    picture.m_pcGroupName = "TABS";
    picture.m_bLockStatus = true;
    auto w = read();
    assert(w.value == "1" && !w.enabled && w.actions.empty());
    picture.m_bLockStatus = false;
    w = read();
    assert(w.enabled && w.actions == std::vector<std::string>{"press"});
    picture.commands = false;
    assert(read().actions.empty());
    picture.commands = true;
    // The image's source: the atlas entry, else the texture and the rectangle shown.
    picture.m_pcGroupName = "PERK_ENABLE";
    picture.m_mobilePictureName = "BasicDefense";
    w = read();
    assert(w.images.front().source.kind == "atlas" && w.images.front().source.group == "PERK_ENABLE" &&
           w.images.front().source.image == "BasicDefense");
    picture.m_mobilePictureName.clear(); // edited UVs: no longer the atlas entry
    w = read();
    assert(w.images.front().source.kind == "texture" && w.images.front().source.image == "interfaces/tabs" &&
           w.images.front().source.uv[2] == .5f && w.images.front().source.uv[3] == .5f);
    // A video or no texture: nothing to show.
    picture.m_pTex = &picture;
    std::vector<MobileWidget> hidden;
    MobilePictureCollect(picture, screen, hidden);
    picture.m_pTex = nullptr;
    picture.m_mobileTexturePath.clear();
    MobilePictureCollect(picture, screen, hidden);
    assert(hidden.empty());
    picture.m_nodeName = "SAVEIMG1";
    picture.m_bMakeBlind = true;
    w = read();
    assert(w.type == "save" && w.index == 0 && w.value == "1" && w.enabled);
    picture.m_nodeName = "SAVEIMG10";
    picture.m_bLockStatus = true;
    w = read();
    assert(w.type == "save" && w.index == 9 && !w.enabled);
    assert(MobileSaveSlot("SAVEIMG11") == -1 && MobileSaveSlot(nullptr) == -1);
    std::cout << "PASS picture collection: tabs, image source atlas/texture, video, lock and save slots\n";
}
