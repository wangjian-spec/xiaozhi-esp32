#include "renderer.h"

#include <algorithm>

#include "widget.h"

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

void LayoutEngine::LayoutTree(Widget* root, const Rect& area) {
    if (!root) {
        return;
    }
    LayoutRecursive(root, area);
}

void LayoutEngine::LayoutRecursive(Widget* node, const Rect& area) {
    if (!node) {
        return;
    }
    node->Measure({area.w, area.h});
    node->Layout(area);

    const Rect parent_rect = node->RectInParent();
    for (const auto& child : node->Children()) {
        Rect child_rect = child->RectInParent();
        if (child_rect.IsEmpty()) {
            child_rect = {0, 0, parent_rect.w, parent_rect.h};
        }
        LayoutRecursive(child.get(), child_rect);
    }
}

void RenderList::Clear() {
    items_.clear();
}

void RenderList::Build(Widget* root) {
    items_.clear();
    uint32_t order = 0;
    Traverse(root, 0, order);
    std::stable_sort(items_.begin(), items_.end(), [](const RenderObject& a, const RenderObject& b) {
        if (a.depth != b.depth) {
            return a.depth < b.depth;
        }
        if (a.z != b.z) {
            return a.z < b.z;
        }
        return a.order < b.order;
    });
}

const std::vector<RenderObject>& RenderList::Items() const {
    return items_;
}

void RenderList::Traverse(Widget* node, uint16_t depth, uint32_t& order) {
    if (!node || !node->Visible()) {
        return;
    }
    RenderObject obj;
    obj.rect = node->RectInWindow();
    obj.widget = node;
    obj.z = node->ZOrder();
    obj.depth = depth;
    obj.order = order++;
    items_.push_back(obj);

    for (const auto& child : node->Children()) {
        Traverse(child.get(), static_cast<uint16_t>(depth + 1), order);
    }
}

const RenderCapabilities& Renderer::Capabilities() const {
    return caps_;
}

void Renderer::Render(RenderList& list, DirtyTracker& dirty, Painter& painter) {
    if (!dirty.HasDirty()) {
        return;
    }

    if (dirty.RequireFullRefresh()) {
        FullRefresh();
    } else {
        PartialRefresh(dirty.Merge());
    }

    for (const auto& item : list.Items()) {
        if (!item.widget) {
            continue;
        }
        painter.PushClip(item.rect);
        painter.SetTransform({item.rect.x, item.rect.y});
        item.widget->Draw(painter);
        painter.SetTransform({0, 0});
        painter.PopClip();
    }

    dirty.Clear();
}

void Renderer::PartialRefresh(const Rect&) {
}

void Renderer::FullRefresh() {
}

} // namespace app_ui
