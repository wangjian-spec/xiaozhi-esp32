#pragma once

#include "geometry.h"

namespace app_ui {

class Widget;

class LayoutEngine {
public:
    void LayoutTree(Widget* root, const Rect& area);

private:
    void LayoutRecursive(Widget* node, const Rect& area);
};

} // namespace app_ui
