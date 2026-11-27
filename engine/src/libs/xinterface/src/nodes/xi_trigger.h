#pragma once

#include "../hover_trigger.hpp"
#include "../inode.h"

// TRIGGER: times the hover triggers of the interface's nodes (hover_trigger.hpp) and draws the time running out as a
// circle filling up beside the mouse.
class CXI_TRIGGER : public CINODE
{
  public:
    CXI_TRIGGER();
    ~CXI_TRIGGER() override;

    void Draw(bool bSelected, uint32_t Delta_Time) override;
    bool Init(INIFILE *ini1, const char *name1, INIFILE *ini2, const char *name2, VRenderer *rs, XYRECT &hostRect,
              XYPOINT &ScreenSize) override;
    void ReleaseAll() override;
    int CommandExecute(int wActCode) override
    {
        return -1;
    }
    bool IsClick(int buttonID, int32_t xPos, int32_t yPos) override
    {
        return false;
    }
    void MouseThis(float fX, float fY) override
    {
    }
    void ChangePosition(XYRECT &rNewPos) override
    {
    }
    void SaveParametersToIni() override
    {
    }

    // the trigger node under the mouse ("" for none)
    void Hover(const std::string &node, const storm::xi::HoverTrigger::Part &part)
    {
        trigger_.Hover(node, part);
    }

  protected:
    void LoadIni(INIFILE *ini1, const char *name1, INIFILE *ini2, const char *name2) override;

    storm::xi::HoverTrigger trigger_;
    int32_t texture_ = -1;
    float time_ = 1.0f; // seconds, when the scripts give no delay
    uint32_t color_ = 0xFFFFFFFF;
    float radius_ = 16.0f;
    FXYPOINT offset_{};
};
