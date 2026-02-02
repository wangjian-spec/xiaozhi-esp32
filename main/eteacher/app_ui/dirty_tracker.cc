#include "dirty_tracker.h"

namespace app_ui {

namespace {
constexpr size_t kFullRefreshThreshold = 32;
}

void DirtyTracker::Add(const Rect& rect, DirtyReason reason) {
    dirty_.push_back({rect, reason});
}

bool DirtyTracker::HasDirty() const {
    return !dirty_.empty();
}

Rect DirtyTracker::Merge() const {
    Rect merged{};
    for (const auto& item : dirty_) {
        merged = merged.Union(item.rect);
    }
    return merged;
}

bool DirtyTracker::RequireFullRefresh() const {
    if (dirty_.size() >= kFullRefreshThreshold) {
        return true;
    }
    for (const auto& item : dirty_) {
        if (item.reason == DirtyReason::Full) {
            return true;
        }
    }
    return false;
}

void DirtyTracker::Clear() {
    dirty_.clear();
}

} // namespace app_ui
