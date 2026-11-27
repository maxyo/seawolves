#pragma once

#include "../inode.h"
#include "windrose_shape.hpp"

#include <memory>
#include <vector>

class CXI_IMAGE;

// WINDROSE: a ship's speed by the wind's direction (Event_GetWindRosePoints, and Event_GetWindRoseComparePoints for a
// second ship), drawn over a back image and under a fore image, with an optional ring coloured by speed
// (gradient_ring, colours from ReInitSpeedColors).
class CXI_WINDROSE : public CINODE
{
  public:
    CXI_WINDROSE();
    ~CXI_WINDROSE() override;

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
    void ChangePosition(XYRECT &rNewPos) override;
    void SaveParametersToIni() override
    {
    }
    uint32_t MessageProc(int32_t msgcode, MESSAGE &message) override;

  protected:
    void LoadIni(INIFILE *ini1, const char *name1, INIFILE *ini2, const char *name2) override;

  private:
    void ReadPoints(); // asks the scripts for the speeds
    void Place();      // centre and images from the rectangle (inscribed_circle)
    void DrawOutline(const std::vector<float> &speeds, uint32_t fill, uint32_t line);

    int32_t points_ = 1;
    bool inscribed_ = true;
    float cx_ = 0, cy_ = 0, radius_ = 0;
    uint32_t fill_ = 0xFFFFFFFF, line_ = 0xFFFFFFFF, compareFill_ = 0xFFFFFFFF, compareLine_ = 0xFFFFFFFF;
    std::vector<float> speeds_, compare_; // compare_ empty: no second ship
    std::unique_ptr<CXI_IMAGE> back_, fore_;
    bool ring_ = false;
    float ringInner_ = 0, ringOuter_ = 0;
};
