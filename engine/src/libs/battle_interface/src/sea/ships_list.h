#pragma once

#include "bi_defines.h"
#include "vai_objbase.h"

// The ships of one sea scene, kept by the battle interface that owns it (Bind) from its messages. Its ship and
// descriptor pointers are valid only while that interface lives: the scripts delete it in the same frame as the
// sea's ships or before them (DeleteSeaEnvironment, Cabin_Start, the boarding), and then the list answers empty, so
// a reader after the scene (the touch HUD after the frame's entity lifecycle) never reaches a freed ship.
class SHIP_DESCRIBE_LIST
{
  public:
    struct SHIP_DESCR
    {
        bool isMyShip;
        bool isTransferableShip;
        bool isDead;
        int32_t relation;
        int32_t characterIndex;
        ATTRIBUTES *pAttr; // ship attributes
        int32_t maxHP;
        int32_t maxSP;
        int32_t maxCrew;
        int32_t pictureNum;
        int32_t selectPictureNum;
        int32_t textureNum;
        VAI_OBJBASE *pShip;
        uint32_t dwShipColor;

        SHIP_DESCR *next;
    };

  public:
    SHIP_DESCRIBE_LIST();
    ~SHIP_DESCRIBE_LIST();

    void Add(int32_t mainChrIndex, int32_t chIdx, ATTRIBUTES *pChAttr, ATTRIBUTES *pShipAttr, bool bMyShip,
             bool bTransferableShip, int32_t relation, uint32_t dwShipColor);

    // Empties the list for the battle interface `owner`.
    void Bind(entid_t owner);

    SHIP_DESCR *GetMainCharacterShip() const
    {
        return Live() ? mainCharacter : nullptr;
    }

    ATTRIBUTES *GetMainCharacterShipAttr() const
    {
        return Live() ? pMainShipAttr : nullptr;
    }

    void Release(int32_t charIdx);
    void ShipSink(int32_t charIdx);
    void ReleaseAll();
    SHIP_DESCR *FindShip(int32_t idxCharacter) const;

    SHIP_DESCR *GetShipRoot() const
    {
        return Live() ? root : nullptr;
    }

    void Refresh();

  protected:
    bool Live() const;

    entid_t owner = invalid_entity;
    SHIP_DESCR *root;
    SHIP_DESCR *mainCharacter;
    ATTRIBUTES *pMainShipAttr;
};

extern SHIP_DESCRIBE_LIST g_ShipList;
