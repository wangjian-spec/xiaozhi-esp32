#pragma once

#include <vector>

#include "dirty_tracker.h"
#include "geometry.h"

namespace app_ui {

struct RenderCapabilities {
    bool partial_refresh = true;
    bool grayscale = false;
    bool invert = false;
    bool double_buffer = false;
};

class Widget;
class Painter;

struct RenderObject {
    Rect rect;
    Widget* widget = nullptr;
    uint8_t z = 0;
    uint16_t depth = 0;
    float alpha = 1.0f;
    uint32_t order = 0;
};

class RenderList {
public:
    void Clear();
    void Build(Widget* root);
    const std::vector<RenderObject>& Items() const;

private:
    void Traverse(Widget* node, uint16_t depth, uint32_t& order);

    std::vector<RenderObject> items_;
};

class Renderer {
public:
    const RenderCapabilities& Capabilities() const;
    void Render(RenderList& list, DirtyTracker& dirty, Painter& painter);

private:
    void PartialRefresh(const Rect& rect);
    void FullRefresh();

    RenderCapabilities caps_{};
};

} // namespace app_ui
