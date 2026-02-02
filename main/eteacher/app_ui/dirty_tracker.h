#pragma once

#include <vector>
#include "geometry.h"

namespace app_ui {

enum class DirtyReason : uint8_t {
    Visual,
    Layout,
    Full
};

struct DirtyItem {
    Rect rect;
    DirtyReason reason = DirtyReason::Visual;
};

class DirtyTracker {
public:
    void Add(const Rect& rect, DirtyReason reason);
    bool HasDirty() const;
    Rect Merge() const;
    bool RequireFullRefresh() const;
    void Clear();

private:
    std::vector<DirtyItem> dirty_;
};

} // namespace app_ui
