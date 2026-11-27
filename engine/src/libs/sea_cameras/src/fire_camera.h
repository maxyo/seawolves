#pragma once

#include "common_camera.h"
#include "renderer.h"

#include <string>

// The aiming camera of games whose scripts create FIRE_CAMERA: the view from a camera locator of the ship model
// ("cam_r" ...), turned by the player within a quarter turn of the locator's own direction.
class FIRE_CAMERA : public COMMON_CAMERA
{
  public:
    bool Init() override;
    uint64_t ProcessMessage(MESSAGE &message) override;
    void SetPlace(const std::string &locator, float shipAy) override;
    void ProcessStage(Stage stage, uint32_t delta) override
    {
        if (stage == Stage::execute)
            Execute(delta);
    }
    void Save(CSaveLoad *) override
    {
    }
    void Load(CSaveLoad *) override
    {
    }

  private:
    void Execute(uint32_t delta);
    bool Place();

    VRenderer *renderer_ = nullptr;
    std::string locator_;
    bool placed_ = false;
    glm::vec3 eye_{};        // the locator's position on the ship
    float locatorAy_ = 0.0f; // the locator's direction around the ship's vertical
    float ay_ = 0.0f;        // the view's azimuth from the locator's direction
    float ax_ = 0.0f;        // the view's elevation
    bool followAim_ = false; // MSG_FIRE_CAMERA_SET_AIM_PITCH: the elevation is the aim's
    float aimAx_ = 0.0f;
};
