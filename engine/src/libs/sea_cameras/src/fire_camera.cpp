#include "fire_camera.h"

#include "fire_camera_view.hpp"

#include "math_inlines.h"
#include "shared/messages.h"
#include "string_compare.hpp"

#include <spdlog/spdlog.h>

using storm::sea_cameras::WrapAngle;

bool FIRE_CAMERA::Init()
{
    renderer_ = static_cast<VRenderer *>(core.GetService("renderer"));
    return renderer_ != nullptr;
}

uint64_t FIRE_CAMERA::ProcessMessage(MESSAGE &message)
{
    if (message.Long() == MSG_FIRE_CAMERA_SET_AIM_PITCH)
    {
        followAim_ = message.Long() != 0;
        aimAx_ = message.Float();
    }
    return 0;
}

void FIRE_CAMERA::SetPlace(const std::string &locator, float shipAy)
{
    locator_ = locator;
    ay_ = shipAy; // from the ship's heading until Place finds the locator
    placed_ = false;
}

// The locator's place on the ship; the view keeps the azimuth it was given, now from the locator's direction.
bool FIRE_CAMERA::Place()
{
    if (!GetAIObj() && !FindShip())
        return false;
    MODEL *model = GetAIObj()->GetModel();
    for (uint32_t n = 0; NODE *node = model->GetNode(n); n++)
    {
        GEOS::INFO info;
        node->geo->GetInfo(info);
        for (int32_t i = 0; i < info.nlabels; i++)
        {
            GEOS::LABEL label;
            node->geo->GetLabel(i, label);
            if (!storm::iEquals(label.name, locator_))
                continue;
            glm::mat4 m;
            memcpy(storm::math::d3dMatrix(m), label.m, sizeof(m));
            eye_ = storm::math::pos(m);
            const glm::vec3 forward = storm::math::vz(m);
            locatorAy_ = atan2f(forward.x, forward.z);
            ay_ = storm::sea_cameras::AimStartAy(ay_, locatorAy_);
            ax_ = 0.0f;
            placed_ = true;
            return true;
        }
    }
    spdlog::error("aiming camera: the ship of {} has no locator '{}'", to_string(pACharacter->GetAttribute("id")),
                  locator_);
    locator_.clear();
    return false;
}

void FIRE_CAMERA::Execute(uint32_t delta)
{
    if (!isOn() || !isActive() || locator_.empty() || (!placed_ && !Place()))
        return;

    ATTRIBUTES *a = AttributesPointer;
    CONTROL_STATE cs;
    core.Controls->GetControlState("FireCamera_Turn_H", cs);
    ay_ = storm::sea_cameras::AimTurnAy(ay_, cs.fValue, a->GetAttributeAsFloat("SensivityAzimuthAngle"),
                                        a->GetAttributeAsFloat("MouseSensitivity", 1.0f),
                                        a->GetAttributeAsFloat("MouseSpeedAy", 1.0f));
    core.Controls->GetControlState("FireCamera_Turn_V", cs);
    ax_ = followAim_ ? aimAx_ : ax_ - a->GetAttributeAsFloat("SensivityHeightAngle") * cs.fValue;
    ax_ = std::clamp(ax_, a->GetAttributeAsFloat("MinAngleX"), a->GetAttributeAsFloat("MaxAngleX"));

    const glm::mat4 &ship = *GetAIObj()->GetMatrix();
    const glm::vec3 shipForward = storm::math::vz(ship);
    const float shipAy = atan2f(shipForward.x, shipForward.z);
    SetPerspective(a->GetAttributeAsFloat("Perspective"));
    renderer_->SetCamera(storm::math::transformPoint(ship, eye_),
                         glm::vec3(ax_, WrapAngle(shipAy + locatorAy_ + ay_), 0.0f), GetPerspective());
}
