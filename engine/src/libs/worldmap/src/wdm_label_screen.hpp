// The world map's labels on the screen in a frame: where each one's point projects, at which size, and the side of
// its point it takes (wdm_label_layout.hpp). WdmIslands::LRender draws them.
#pragma once

#include "storm_glm.h"
#include "wdm_label_layout.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace storm::worldmap
{
// The labels on the display, in dp (1/160 inch)
constexpr float kLabelGapDp = 4.0f;  // the space labels keep between them
constexpr float kLabelHoldDp = 3.0f; // how much of it a label may lose before it moves to another side
// The smallest size a kind of label shrinks to above its own height (HeightScale): smaller is no longer readable on a
// phone (the town font's line 16 dp -> 13.6 dp)
constexpr float kLabelFarScale = 0.85f;

// A label of the map: a name and/or a picture at a world point, and its state on the screen.
struct MapLabel
{
    std::string text;        // Label text
    glm::vec3 pos;           // Label position
    float width, height;     // The label's size at the font's own size, px: text and picture
    float l, t, r, b;        // Its box on the screen in this frame, px
    bool onScreen;           // That box is on the screen
    float textX, textY;      // Relative text position
    float iconX, iconY;      // The relative position of the picture
    float alpha;             // The current state of the rectangle
    float heightView;        // The camera height above which the label shrinks (the game hid it there)
    float scale;             // Its size on the screen in this frame (of the font's own size)
    int32_t font;            // The renderer's font id
    int32_t icon;            // Image index
    uint32_t weight;         // Which of two colliding labels shows (the heavier)
    bool placed;             // Shows in this frame (on the screen, a name to show)
    LabelSide side;          // Where it lies by its point (wdm_label_layout.hpp)
    std::string id;          // Label ID
    uint32_t idHash;         // Hash value of identifier
    int32_t next;            // Next label on the list
    std::string locatorName; // The name of the locator in which it is located
};

class LabelScreen
{
  public:
    LabelScreen()
    {
        order_.reserve(1024);
    }

    // Places the labels for the view-projection `mtx` on a w x h target, `scale` their size at the camera's own
    // height; the indices of those that show, lighter first (one without a free side lies under the heavier).
    const std::vector<int32_t> &Place(std::vector<MapLabel> &labels, const glm::mat4 &mtx, float w, float h,
                                      float scale, float pxPerDp, float cameraHeight)
    {
        // Projecting to the screen; the labels that may show take part in the layout
        order_.clear();
        boxes_.clear();
        storm::math::ProjectedPoint prjVertex;
        for (int32_t i = 0; i < static_cast<int32_t>(labels.size()); i++)
        {
            MapLabel &label = labels[i];
            storm::math::projectPoints(mtx, &label.pos, &prjVertex, 1, w * 0.5f, h * 0.5f, 0, 0);
            // Perspective divides by clip.w (view-space z). Behind-camera anchors must
            // not reappear mirrored on screen, or keep the last visible rectangle alive.
            if (!(prjVertex.rhw > 0.0f) || !std::isfinite(prjVertex.rhw) || !std::isfinite(prjVertex.x) ||
                !std::isfinite(prjVertex.y))
            {
                label.onScreen = false;
                label.placed = false;
                label.side = LabelSide::None;
                continue;
            }
            // At a side of its world point (wdm_label_layout.hpp); screen HUD never displaces the name.
            label.scale = scale * HeightScale(label.heightView, cameraHeight, kLabelFarScale);
            const float width = label.width * label.scale, height = label.height * label.scale;
            // Under the point until the layout picks its side
            label.l = prjVertex.x - 0.5f * width, label.t = prjVertex.y, label.r = label.l + width,
            label.b = label.t + height;
            // On the screen on any side of the point (the layout picks one below)
            label.onScreen = prjVertex.x - width - kLabelGapDp * pxPerDp < w && prjVertex.y - height < h &&
                             prjVertex.x + width + kLabelGapDp * pxPerDp >= 0.0f && prjVertex.y + height >= 0.0f;
            const bool mayShow = label.onScreen && (!label.text.empty() || label.icon >= 0);
            if (!mayShow)
            {
                label.placed = false;
                label.side = LabelSide::None;
                continue;
            }
            order_.push_back(i);
            boxes_.push_back({prjVertex.x, prjVertex.y, width, height, label.weight, label.side});
        }
        // Names never cover one another where a side of their point is free (wdm_label_layout.hpp)
        layout_.Place(boxes_, kLabelGapDp * pxPerDp, kLabelHoldDp * pxPerDp);
        for (size_t i = 0; i < order_.size(); i++)
        {
            MapLabel &label = labels[order_[i]];
            const LabelBox &box = boxes_[i];
            label.l = box.left, label.t = box.top, label.r = box.right, label.b = box.bottom;
            label.side = box.side;
            label.placed = true;
        }
        // Lighter names first: one without a free side lies under the heavier
        std::stable_sort(order_.begin(), order_.end(),
                         [&](int32_t a, int32_t b) { return labels[a].weight < labels[b].weight; });
        return order_;
    }

  private:
    std::vector<int32_t> order_;
    std::vector<LabelBox> boxes_;
    LabelLayout layout_;
};
} // namespace storm::worldmap
