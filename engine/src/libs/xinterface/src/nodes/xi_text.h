#pragma once

#include "../inode.h"
#include "../rich_text.hpp"

#include <map>
#include <string>
#include <vector>

// TEXT: rich text (rich_text.hpp) wrapped in the node's rectangle. Ini keys: string (a common-strings key), font,
// color, fontScale, globalScale, lineSpace (0: the font's height), adaptiveLineHeight, alignment (left, center,
// right), valignment (top, center, bottom), vAnchor (top, bottom), scrollerName.
class CXI_TEXT : public CINODE
{
  public:
    CXI_TEXT();
    ~CXI_TEXT() override;

    void Draw(bool bSelected, uint32_t Delta_Time) override;
    bool Init(INIFILE *ini1, const char *name1, INIFILE *ini2, const char *name2, VRenderer *rs, XYRECT &hostRect,
              XYPOINT &ScreenSize) override;
    void ReleaseAll() override;
    int CommandExecute(int wActCode) override;
    bool IsClick(int buttonID, int32_t xPos, int32_t yPos) override;
    void MouseThis(float fX, float fY) override
    {
    }
    void ChangePosition(XYRECT &rNewPos) override;
    void SaveParametersToIni() override
    {
    }
    uint32_t MessageProc(int32_t msgcode, MESSAGE &message) override;

    void SetText(std::string text);
    // the widest line and the height of the text laid out `width` wide
    std::pair<float, float> Measure(float width) const;

  protected:
    void LoadIni(INIFILE *ini1, const char *name1, INIFILE *ini2, const char *name2) override;

  private:
    std::vector<storm::xi::RichLine> Layout(const std::vector<storm::xi::RichItem> &items, float width) const;
    std::vector<storm::xi::RichLine> Layout(const std::vector<storm::xi::RichItem> &items) const
    {
        return Layout(items, static_cast<float>(m_rect.right - m_rect.left));
    }
    int32_t Font(const std::string &name) const; // a [font=] of the text, else the node's
    // a [scale=] replaces fontScale; globalScale applies to both
    float Scale(const storm::xi::RichStyle &style) const
    {
        return style.scale.value_or(scale_) * globalScale_;
    }
    // lines are lineSpace high whatever the scale; without it, as high as the font's glyphs (20 without a font)
    int32_t LineSpace() const
    {
        return lineSpace_ > 0 ? lineSpace_ : font_ >= 0 ? m_rs->CharHeight(font_) : 20;
    }
    void DrawIcon(const storm::xi::RichIcon &icon, float x, float y, float scale);

    std::string raw_;
    std::vector<storm::xi::RichItem> items_;
    std::vector<storm::xi::RichLine> lines_;
    mutable std::map<std::string, int32_t> fonts_; // fonts the text names, loaded once
    int32_t font_ = -1;
    uint32_t color_ = 0xFFFFFFFF;
    float scale_ = 1.0f, globalScale_ = 1.0f;
    int32_t lineSpace_ = 0;
    bool adaptive_ = false;
    int32_t align_ = PR_ALIGN_LEFT;
    int32_t valign_ = 0; // 0 top, 1 center, 2 bottom
    bool anchorBottom_ = true;
    std::string scroller_;
    float scroll_ = 0.0f; // 0..1 of the part that does not fit
    bool frozen_ = false; // commands frozen (message 10)
};
