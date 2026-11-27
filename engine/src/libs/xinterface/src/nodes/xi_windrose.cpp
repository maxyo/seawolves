#include "xi_windrose.h"

#include "core.h"
#include "speed_colors.hpp"
#include "v_data.h"
#include "xi_image.h"

#include <spdlog/spdlog.h>

CXI_WINDROSE::CXI_WINDROSE()
{
    m_nNodeType = NODETYPE_WINDROSE;
}

CXI_WINDROSE::~CXI_WINDROSE()
{
    ReleaseAll();
}

bool CXI_WINDROSE::Init(INIFILE *ini1, const char *name1, INIFILE *ini2, const char *name2, VRenderer *rs,
                        XYRECT &hostRect, XYPOINT &ScreenSize)
{
    return CINODE::Init(ini1, name1, ini2, name2, rs, hostRect, ScreenSize);
}

void CXI_WINDROSE::LoadIni(INIFILE *ini1, const char *name1, INIFILE *ini2, const char *name2)
{
    char param[512];
    points_ = std::max<int32_t>(1, GetIniLong(ini1, name1, ini2, name2, "points", 1));
    inscribed_ = GetIniBool(ini1, name1, ini2, name2, "inscribed_circle", false);
    if (!inscribed_)
    {
        const XYPOINT pos = GetIniLongPoint(ini1, name1, ini2, name2, "pos", XYPOINT(-1, -1));
        radius_ = GetIniFloat(ini1, name1, ini2, name2, "radius", -1.0f);
        cx_ = static_cast<float>(pos.x);
        cy_ = static_cast<float>(pos.y);
        // without a centre or a radius the circle is the one inscribed in the node
        inscribed_ = (pos.x == -1 && pos.y == -1) || radius_ < 0.0f;
    }
    fill_ = GetIniARGB(ini1, name1, ini2, name2, "fill_color", 0xFFFFFFFF);
    line_ = GetIniARGB(ini1, name1, ini2, name2, "line_color", 0xFFFFFFFF);
    compareFill_ = GetIniARGB(ini1, name1, ini2, name2, "compare_fill_color", 0xFFFFFFFF);
    compareLine_ = GetIniARGB(ini1, name1, ini2, name2, "compare_line_color", 0xFFFFFFFF);
    for (auto [key, image] : {std::pair{"backimage", &back_}, std::pair{"foreimage", &fore_}})
        if (ReadIniString(ini1, name1, ini2, name2, key, param, sizeof(param), ""))
        {
            *image = std::make_unique<CXI_IMAGE>();
            (*image)->LoadAccordingToString(param);
        }
    if (ReadIniString(ini1, name1, ini2, name2, "gradient_ring", param, sizeof(param), ""))
    {
        const XYPOINT ring = GetIniLongPoint(ini1, name1, ini2, name2, "gradient_ring", XYPOINT(0, 0));
        ring_ = true;
        ringInner_ = static_cast<float>(ring.x);
        ringOuter_ = static_cast<float>(ring.y);
    }
    Place();
    ReadPoints();
}

void CXI_WINDROSE::ReleaseAll()
{
    back_.reset();
    fore_.reset();
}

void CXI_WINDROSE::ChangePosition(XYRECT &rNewPos)
{
    m_rect = rNewPos;
    Place();
}

void CXI_WINDROSE::Place()
{
    if (inscribed_)
    {
        cx_ = static_cast<float>((m_rect.left + m_rect.right) / 2);
        cy_ = static_cast<float>((m_rect.top + m_rect.bottom) / 2);
        radius_ = static_cast<float>(m_rect.bottom - m_rect.top) * 0.5f;
    }
    for (CXI_IMAGE *image : {back_.get(), fore_.get()})
        if (image)
            image->SetPosition(static_cast<int32_t>(cx_), static_cast<int32_t>(cy_), IPType_Center);
}

void CXI_WINDROSE::ReadPoints()
{
    const auto read = [this](const char *event, std::vector<float> &speeds) {
        speeds.clear();
        VDATA *result = core.Event(event, "s", m_nodeName);
        if (!result)
            return false;
        auto *array = result->IsReference() ? reinterpret_cast<VDATA *>(result->GetVarPointer()) : result;
        if (!array || !array->IsArray() || array->GetElementsNum() < static_cast<uint32_t>(points_))
        {
            spdlog::error("WINDROSE {}: {} gave no array of {} speeds", m_nodeName, event, points_);
            return false;
        }
        speeds.resize(points_);
        for (int32_t i = 0; i < points_; ++i)
            array->Get(speeds[i], static_cast<uint32_t>(i));
        return true;
    };
    if (!read("Event_GetWindRosePoints", speeds_))
        speeds_.assign(points_, 0.0f);
    read("Event_GetWindRoseComparePoints", compare_);
}

uint32_t CXI_WINDROSE::MessageProc(int32_t msgcode, MESSAGE &message)
{
    switch (msgcode)
    {
    case 0: { // position
        XYRECT r;
        r.left = message.Long();
        r.top = message.Long();
        r.right = message.Long();
        r.bottom = message.Long();
        ChangePosition(r);
    }
    break;
    case 1: // the speeds again
        ReadPoints();
        break;
    case 2:
        fill_ = message.Long();
        break;
    case 3:
        line_ = message.Long();
        break;
    }
    return 0;
}

void CXI_WINDROSE::DrawOutline(const std::vector<float> &speeds, uint32_t fill, uint32_t line)
{
    const auto outline = storm::xi::WindRoseOutline(cx_, cy_, radius_, speeds);
    if (outline.size() < 3)
        return;
    std::vector<XI_NOTEX_VERTEX> vertices{{{cx_, cy_, 1.f}, fill}};
    for (const auto &p : outline)
        vertices.push_back({{p.x, p.y, 1.f}, fill});
    m_rs->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, XI_NOTEX_FVF, static_cast<uint32_t>(vertices.size() - 2), vertices.data(),
                          sizeof(XI_NOTEX_VERTEX), "iRectangle");
    vertices.erase(vertices.begin());
    for (auto &v : vertices)
        v.color = line;
    m_rs->DrawPrimitiveUP(D3DPT_LINESTRIP, XI_NOTEX_FVF, static_cast<uint32_t>(vertices.size() - 1), vertices.data(),
                          sizeof(XI_NOTEX_VERTEX), "iRectangle");
}

void CXI_WINDROSE::Draw(bool, uint32_t)
{
    if (!m_bUse)
        return;
    if (back_)
        back_->Draw();
    DrawOutline(speeds_, fill_, line_);
    if (!compare_.empty())
        DrawOutline(compare_, compareFill_, compareLine_);
    if (ring_)
    {
        const auto ring = storm::xi::WindRoseRing(cx_, cy_, ringInner_, ringOuter_, speeds_, storm::SpeedColor);
        std::vector<XI_NOTEX_VERTEX> vertices;
        vertices.reserve(ring.size());
        for (const auto &v : ring)
            vertices.push_back({{v.x, v.y, 1.f}, v.color});
        if (!vertices.empty())
            m_rs->DrawPrimitiveUP(D3DPT_TRIANGLELIST, XI_NOTEX_FVF, static_cast<uint32_t>(vertices.size() / 3),
                                  vertices.data(), sizeof(XI_NOTEX_VERTEX), "iRectangle");
    }
    if (fore_)
        fore_->Draw();
}
