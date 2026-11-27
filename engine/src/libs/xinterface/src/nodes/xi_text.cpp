#include "xi_text.h"

#include "../string_service.h"
#include "../vx_service.h"
#include "string_compare.hpp"
#include "xi_scroller.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <numeric>

CXI_TEXT::CXI_TEXT()
{
    m_nNodeType = NODETYPE_TEXT;
}

CXI_TEXT::~CXI_TEXT()
{
    ReleaseAll();
}

bool CXI_TEXT::Init(INIFILE *ini1, const char *name1, INIFILE *ini2, const char *name2, VRenderer *rs, XYRECT &hostRect,
                    XYPOINT &ScreenSize)
{
    return CINODE::Init(ini1, name1, ini2, name2, rs, hostRect, ScreenSize);
}

void CXI_TEXT::LoadIni(INIFILE *ini1, const char *name1, INIFILE *ini2, const char *name2)
{
    char param[1024];
    if (ReadIniString(ini1, name1, ini2, name2, "font", param, sizeof(param), ""))
        font_ = m_rs->LoadFont(param);
    color_ = GetIniARGB(ini1, name1, ini2, name2, "color", 0xFFFFFFFF);
    scale_ = GetIniFloat(ini1, name1, ini2, name2, "fontScale", 1.0f);
    globalScale_ = GetIniFloat(ini1, name1, ini2, name2, "globalScale", 1.0f);
    lineSpace_ = GetIniLong(ini1, name1, ini2, name2, "lineSpace", 0);
    adaptive_ = GetIniLong(ini1, name1, ini2, name2, "adaptiveLineHeight", 0) != 0;
    const auto keyword = [&](const char *key, const char *fallback) {
        ReadIniString(ini1, name1, ini2, name2, key, param, sizeof(param), fallback);
        return std::string(param);
    };
    const std::string align = keyword("alignment", "left");
    align_ = storm::iEquals(align, "center")  ? PR_ALIGN_CENTER
             : storm::iEquals(align, "right") ? PR_ALIGN_RIGHT
                                              : PR_ALIGN_LEFT;
    const std::string valign = keyword("valignment", "top");
    valign_ = storm::iEquals(valign, "center") ? 1 : storm::iEquals(valign, "bottom") ? 2 : 0;
    anchorBottom_ = !storm::iEquals(keyword("vAnchor", "bottom"), "top");
    scroller_ = keyword("scrollerName", "");
    const std::string key = keyword("string", "");
    if (!key.empty())
    {
        const char *text = ptrOwner->StringService()->GetString(key.c_str());
        if (text)
            SetText(text);
        else
            spdlog::error("TEXT {}: no string '{}'", m_nodeName, key);
    }
}

void CXI_TEXT::ReleaseAll()
{
    for (const auto &[name, id] : fonts_)
        if (id >= 0)
            m_rs->UnloadFont(id);
    fonts_.clear();
    if (font_ >= 0)
        m_rs->UnloadFont(font_);
    font_ = -1;
}

int32_t CXI_TEXT::Font(const std::string &name) const
{
    if (name.empty())
        return font_;
    auto found = fonts_.find(name);
    if (found == fonts_.end())
        found = fonts_.emplace(name, m_rs->LoadFont(name.c_str())).first;
    return found->second >= 0 ? found->second : font_;
}

std::vector<storm::xi::RichLine> CXI_TEXT::Layout(const std::vector<storm::xi::RichItem> &items, float width) const
{
    const float scale = scale_ * globalScale_;
    const storm::xi::RichMeasure measure{
        [&](std::string_view text, const storm::xi::RichStyle &style) {
            return static_cast<float>(m_rs->StringWidth(text, Font(style.font), scale * style.scale));
        },
        [&](const storm::xi::RichStyle &style) { return m_rs->CharHeight(Font(style.font)) * scale * style.scale; }};
    const float lineSpace = lineSpace_ > 0 ? lineSpace_ * globalScale_ : m_rs->CharHeight(font_) * scale;
    return storm::xi::LayoutRichText(items, width, measure, lineSpace, adaptive_);
}

std::pair<float, float> CXI_TEXT::Measure(float width) const
{
    float widest = 0, height = 0;
    for (const auto &line : Layout(items_, width))
    {
        widest = std::max(widest, line.width);
        height += line.height;
    }
    return {widest, height};
}

void CXI_TEXT::SetText(std::string text)
{
    raw_ = std::move(text);
    items_ = storm::xi::ParseRichText(
        raw_, [this](std::string_view tag) { spdlog::warn("TEXT {}: unknown tag {}", m_nodeName, tag); });
    lines_ = Layout(items_);
    scroll_ = 0.0f;
}

void CXI_TEXT::ChangePosition(XYRECT &rNewPos)
{
    m_rect = rNewPos;
    lines_ = Layout(items_);
}

void CXI_TEXT::Draw(bool, uint32_t)
{
    if (!m_bUse || lines_.empty())
        return;
    const float height = static_cast<float>(m_rect.bottom - m_rect.top);
    const float total = std::accumulate(lines_.begin(), lines_.end(), 0.0f,
                                        [](float sum, const storm::xi::RichLine &line) { return sum + line.height; });
    float y = static_cast<float>(m_rect.top);
    if (total <= height)
        y += valign_ == 1 ? (height - total) / 2 : valign_ == 2 ? height - total : 0.0f;
    else if (!scroller_.empty() || scroll_ > 0.0f)
        y -= (total - height) * scroll_;
    else if (anchorBottom_)
        y -= total - height; // what does not fit is cut at the top
    const float scale = scale_ * globalScale_;
    for (const auto &line : lines_)
    {
        if (y + line.height > m_rect.top - 1 && y < m_rect.bottom + 1)
        {
            const float left = align_ == PR_ALIGN_CENTER  ? m_rect.left + (m_rect.right - m_rect.left - line.width) / 2
                               : align_ == PR_ALIGN_RIGHT ? m_rect.right - line.width
                                                          : static_cast<float>(m_rect.left);
            for (const auto &piece : line.pieces)
            {
                const float x = left + piece.x;
                if (const auto *icon = std::get_if<storm::xi::RichIcon>(&items_[piece.item]))
                {
                    DrawIcon(*icon, x, y + (line.height - icon->height) / 2 + icon->voffset, 1.0f);
                    continue;
                }
                const auto &text = std::get<storm::xi::RichText>(items_[piece.item]);
                const int32_t font = Font(text.style.font);
                const float glyphs = m_rs->CharHeight(font) * scale * text.style.scale;
                m_rs->ExtPrint(font, text.style.color_set ? text.style.color : color_, 0, PR_ALIGN_LEFT, true,
                               scale * text.style.scale, m_screenSize.x, m_screenSize.y, static_cast<int32_t>(x),
                               static_cast<int32_t>(y + line.height - glyphs + text.style.voffset), "%s",
                               piece.text.c_str());
            }
        }
        y += line.height;
    }
}

void CXI_TEXT::DrawIcon(const storm::xi::RichIcon &icon, float x, float y, float)
{
    VXSERVICE *pictures = ptrOwner->PictureService();
    const int32_t texture = pictures->GetTextureID(icon.group.c_str());
    FXYRECT uv;
    if (texture < 0 || !pictures->GetTexturePos(icon.group.c_str(), icon.picture.c_str(), uv))
    {
        spdlog::error("TEXT {}: no picture {} of {}", m_nodeName, icon.picture, icon.group);
        if (texture >= 0)
            pictures->ReleaseTextureID(icon.group.c_str());
        return;
    }
    const float w = static_cast<float>(icon.width), h = static_cast<float>(icon.height);
    XI_ONETEX_VERTEX v[4] = {{{x, y, 1.0f}, icon.color, uv.left, uv.top},
                             {{x, y + h, 1.0f}, icon.color, uv.left, uv.bottom},
                             {{x + w, y, 1.0f}, icon.color, uv.right, uv.top},
                             {{x + w, y + h, 1.0f}, icon.color, uv.right, uv.bottom}};
    m_rs->TextureSet(0, texture);
    m_rs->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, XI_ONETEX_FVF, 2, v, sizeof(XI_ONETEX_VERTEX), "iVideo");
    pictures->ReleaseTextureID(icon.group.c_str());
}

int CXI_TEXT::CommandExecute(int wActCode)
{
    return frozen_ ? -1 : 0;
}

bool CXI_TEXT::IsClick(int buttonID, int32_t xPos, int32_t yPos)
{
    return !frozen_ && m_bClickable && xPos >= m_rect.left && xPos <= m_rect.right && yPos >= m_rect.top &&
           yPos <= m_rect.bottom;
}

uint32_t CXI_TEXT::MessageProc(int32_t msgcode, MESSAGE &message)
{
    switch (msgcode)
    {
    case 0: // the text, raw
        SetText(message.String());
        break;
    case 1: { // the text of a common-strings key
        const std::string key = message.String();
        const char *text = ptrOwner->StringService()->GetString(key.c_str());
        if (!text)
            spdlog::error("TEXT {}: no string '{}'", m_nodeName, key);
        SetText(text ? text : key);
        break;
    }
    case 2: { // font
        const std::string name = message.String();
        if (font_ >= 0)
            m_rs->UnloadFont(font_);
        font_ = m_rs->LoadFont(name.c_str());
        lines_ = Layout(items_);
        break;
    }
    case 3: // colour
        color_ = static_cast<uint32_t>(message.Long());
        break;
    case 4: // scale
        scale_ = message.Float();
        lines_ = Layout(items_);
        break;
    case 5: // line space
        lineSpace_ = message.Long();
        lines_ = Layout(items_);
        break;
    case 6: // adaptive line height
        adaptive_ = message.Long() != 0;
        lines_ = Layout(items_);
        break;
    case 7: // alignment (the renderer's PR_ALIGN_*)
        align_ = message.Long();
        break;
    case 8: // append raw text
        SetText(raw_ + message.String());
        break;
    case 9: { // scroll position 0..1
        scroll_ = std::clamp(message.Float(), 0.0f, 1.0f);
        if (auto *scroller = scroller_.empty() ? nullptr : ptrOwner->FindNode(scroller_.c_str(), nullptr))
            if (scroller->m_nNodeType == NODETYPE_SCROLLER)
                static_cast<CXI_SCROLLER *>(scroller)->SetRollerPos(scroll_);
        break;
    }
    case 10: // freeze the node's commands
        frozen_ = message.Long() != 0;
        break;
    case 11: // lines of the text
        return static_cast<uint32_t>(lines_.size());
    case 12: { // lines the given text would take
        const char *text = nullptr;
        message.ScriptVariablePointer()->Get(text);
        return static_cast<uint32_t>(
            Layout(storm::xi::ParseRichText(text ? text : "", [](std::string_view) {})).size());
    }
    default:
        spdlog::error("TEXT {}: no message {}", m_nodeName, msgcode);
        break;
    }
    return 0;
}
