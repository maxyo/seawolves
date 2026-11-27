#pragma once

#include "common_camera.h"

#include <spdlog/spdlog.h>

// Caribbean Legend's aiming camera (FIRE_CAMERA, its scripts' SeaFireCamera) is not implemented: the entity exists for
// the scripts that create it with the other sea cameras, and SEA_CAMERAS refuses to switch to it.
class FIRE_CAMERA : public COMMON_CAMERA
{
  public:
    bool Init() override
    {
        return true;
    }
    bool CanBeSwitchedTo() const override
    {
        spdlog::error("sea cameras: the aiming camera (FIRE_CAMERA) is not implemented, the current camera stays");
        return false;
    }
    uint64_t ProcessMessage(MESSAGE &) override
    {
        return 0; // MSG_FIRE_CAMERA_SET_AIM_PITCH from the spyglass: no camera to aim
    }
    void ProcessStage(Stage, uint32_t) override
    {
    }
    void Save(CSaveLoad *) override
    {
    }
    void Load(CSaveLoad *) override
    {
    }
};
