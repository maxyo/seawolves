#pragma once

#include "shared/sea_ai/script_defines.h"

#include <string>

class AIShip;

class AIShipCameraController
{
  private:
    ATTRIBUTES *pTargetAPointer;
    uint32_t dwTarget;
    float fDelta;
    int32_t iCrosshairTex;
    bool bCameraOutside;

    uint32_t Colors[RELATION_ALLIANCE + 1];
    std::string aimingBoard_; // the board the aiming camera fires, empty outside it

    uint32_t dwSubTexturesX, dwSubTexturesY;

    AIShip *pOurAIShip;
    ATTRIBUTES *pACrosshair;

    bool isCameraOutside() const
    {
        return bCameraOutside;
    };

  public:
    void Realize(float fDeltaTime);
    void Execute(float fDeltaTime);

    bool Fire();
    bool Init();

    // AI_MESSAGE_FIRE_CAMERA_RESET: the aiming camera aims the board the scripts name (GetCurBort); _EXIT: it is left.
    void EnterAimingCamera();
    void LeaveAimingCamera();

    AIShipCameraController(AIShip *);
    ~AIShipCameraController();

    void SetAIShip(AIShip *pShip)
    {
        pOurAIShip = pShip;
    }

    AIShip *GetAIShip() const
    {
        return pOurAIShip;
    }

    void Save(CSaveLoad *pSL) const;
    void Load(CSaveLoad *pSL);
};
