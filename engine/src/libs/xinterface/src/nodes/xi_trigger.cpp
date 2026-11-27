#include "xi_trigger.h"

#include "core.h"
#include "v_data.h"

#include <cmath>

namespace
{
constexpr int kSegments = 32;
}

CXI_TRIGGER::CXI_TRIGGER()
    : trigger_({[this] {
                    VDATA *delay = core.Event("Event_GetTriggerDelay");
                    return delay ? delay->GetFloat() : time_;
                },
                [](const std::string &node) { core.Event("Event_NodeStartTrigger", "s", node.c_str()); },
                [](const std::string &node) { core.Event("Event_NodeEndTrigger", "s", node.c_str()); },
                [] { core.Event("Event_NullTrigger"); },
                [](const std::string &node, const storm::xi::HoverTrigger::Part &part) {
                    core.Event("Event_ChangeTableTriggerFrame", "slllllll", node.c_str(), part.line, part.column,
                               part.left, part.top, part.right, part.bottom);
                }})
{
    m_nNodeType = NODETYPE_TRIGGER;
}

CXI_TRIGGER::~CXI_TRIGGER()
{
    ReleaseAll();
}

bool CXI_TRIGGER::Init(INIFILE *ini1, const char *name1, INIFILE *ini2, const char *name2, VRenderer *rs,
                       XYRECT &hostRect, XYPOINT &ScreenSize)
{
    return CINODE::Init(ini1, name1, ini2, name2, rs, hostRect, ScreenSize);
}

void CXI_TRIGGER::LoadIni(INIFILE *ini1, const char *name1, INIFILE *ini2, const char *name2)
{
    char param[255];
    if (ReadIniString(ini1, name1, ini2, name2, "texture", param, sizeof(param), ""))
        texture_ = m_rs->TextureCreate(param);
    time_ = GetIniFloat(ini1, name1, ini2, name2, "time", 1.0f);
    color_ = GetIniARGB(ini1, name1, ini2, name2, "color", 0xFFFFFFFF);
    radius_ = GetIniFloat(ini1, name1, ini2, name2, "radius", 16.0f);
    offset_.x = GetIniFloat(ini1, name1, ini2, name2, "offset_x", 0.0f);
    offset_.y = GetIniFloat(ini1, name1, ini2, name2, "offset_y", 0.0f);
}

void CXI_TRIGGER::ReleaseAll()
{
    if (texture_ >= 0)
        m_rs->TextureRelease(texture_);
    texture_ = -1;
}

void CXI_TRIGGER::Draw(bool, uint32_t Delta_Time)
{
    trigger_.Advance(Delta_Time / 1000.0f);
    const float progress = trigger_.Progress();
    if (!m_bUse || progress <= 0.0f)
        return;
    // a fan from the top, clockwise, over the part of the circle that has passed
    const FXYPOINT mouse = ptrOwner->GetMousePoint();
    const float cx = mouse.x + offset_.x, cy = mouse.y + offset_.y;
    XI_ONETEX_VERTEX fan[kSegments + 2];
    const int segments = std::max(1, static_cast<int>(std::ceil(progress * kSegments)));
    fan[0] = {{cx, cy, 1.0f}, color_, 0.5f, 0.5f};
    for (int i = 0; i <= segments; ++i)
    {
        const float angle = 2.0f * 3.14159265f * std::min(progress, static_cast<float>(i) / kSegments);
        const float dx = std::sin(angle), dy = -std::cos(angle);
        fan[i + 1] = {{cx + dx * radius_, cy + dy * radius_, 1.0f}, color_, 0.5f + dx * 0.5f, 0.5f + dy * 0.5f};
    }
    m_rs->TextureSet(0, texture_);
    m_rs->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, XI_ONETEX_FVF, segments, fan, sizeof(XI_ONETEX_VERTEX), "iVideo");
}
