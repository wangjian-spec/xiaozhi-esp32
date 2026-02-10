#include "renderer.h"

// 渲染器实现
// 本文件实现 UI 渲染相关功能（脏区域跟踪、绘制器封装等），负责将 Widget 树绘制到 EPD/画布。
// 与 UI 布局描述的来源无关。

#include <algorithm>
#include <string_view>

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/font_manager/font_manager.h"
#include "widget.h"
#include "debug.h"

namespace app_ui {

namespace {
constexpr size_t kFullRefreshThreshold = 32;
constexpr size_t kMaxDirtyItems = 64;
constexpr const char* kStatusFont = "wenquanyi_9pt";

bool RectEquals(const Rect& a, const Rect& b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

Rect RectIntersect(const Rect& a, const Rect& b) {
    const int16_t x1 = (a.x > b.x) ? a.x : b.x;
    const int16_t y1 = (a.y > b.y) ? a.y : b.y;
    const int16_t x2 = ((a.x + a.w) < (b.x + b.w)) ? (a.x + a.w) : (b.x + b.w);
    const int16_t y2 = ((a.y + a.h) < (b.y + b.h)) ? (a.y + a.h) : (b.y + b.h);
    const int16_t w = static_cast<int16_t>(x2 - x1);
    const int16_t h = static_cast<int16_t>(y2 - y1);
    if (w <= 0 || h <= 0) {
        return {0, 0, 0, 0};
    }
    return {x1, y1, w, h};
}

int GetFontHeight(std::string_view name) {
    const auto* font = eteacher::font_manager::GetBuiltinFont(name);
    if (!font) {
        return 16;
    }
    return static_cast<int>(font->Header().ascent + font->Header().descent);
}

int GetFontAscent(std::string_view name) {
    const auto* font = eteacher::font_manager::GetBuiltinFont(name);
    if (!font) {
        return 12;
    }
    return static_cast<int>(font->Header().ascent);
}
}

void DirtyTracker::Add(const Rect& rect, DirtyReason reason) {
    if (dirty_.size() >= kMaxDirtyItems) {
        dirty_.clear();
        Rect full = full_rect_.IsEmpty() ? rect : full_rect_;
        dirty_.push_back({full, DirtyReason::Full});
        return;
    }
    dirty_.push_back({rect, reason});
}

EpdPainter::EpdPainter(::CustomEpdDisplay* epd, ::Adafruit_GFX& gfx)
    : epd_(epd), gfx_(gfx) {
    draw_color_ = GxEPD_BLACK;
    text_color_ = GxEPD_BLACK;
}

void EpdPainter::SetClip(const Rect&) {}

void EpdPainter::PushClip(const Rect&) {}

void EpdPainter::PopClip() {}

void EpdPainter::DrawText(Point p, const char* text) {
    if (!epd_ || !text) {
        return;
    }
    const int16_t x = static_cast<int16_t>(p.x + offset_.x);
    const int16_t y = static_cast<int16_t>(p.y + offset_.y);
    epd_->DrawUtf8(x, y + GetFontAscent(kStatusFont), text, kStatusFont, text_color_);
}

Size EpdPainter::MeasureText(const char* text, Font*) {
    if (!epd_ || !text) {
        return {0, 0};
    }
    const int16_t w = epd_->MeasureUtf8Width(text, kStatusFont);
    return {w, static_cast<int16_t>(GetFontHeight(kStatusFont))};
}

void EpdPainter::DrawRect(const Rect& rect) {
    gfx_.drawRect(rect.x + offset_.x, rect.y + offset_.y, rect.w, rect.h, draw_color_);
}

void EpdPainter::FillRect(const Rect& rect) {
    gfx_.fillRect(rect.x + offset_.x, rect.y + offset_.y, rect.w, rect.h, draw_color_);
}

void EpdPainter::DrawImage(Point, const Image*) {}

void EpdPainter::SetFont(Font*) {}

void EpdPainter::SetDrawColor(Color color) {
    draw_color_ = (color == Color::Black) ? GxEPD_BLACK : GxEPD_WHITE;
}

void EpdPainter::SetTextColor(Color color) {
    text_color_ = (color == Color::Black) ? GxEPD_BLACK : GxEPD_WHITE;
}

void EpdPainter::DrawCircle(Point center, int radius) {
    gfx_.drawCircle(center.x + offset_.x, center.y + offset_.y, radius, draw_color_);
}

void EpdPainter::SetTransform(const Point& offset) {
    offset_ = offset;
}

void EpdPainter::SetAlpha(float) {}

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

bool DirtyTracker::Intersects(const Rect& rect) const {
    for (const auto& item : dirty_) {
        if (item.rect.Intersects(rect)) {
            return true;
        }
    }
    return false;
}

const std::vector<DirtyItem>& DirtyTracker::Items() const {
    return dirty_;
}

void DirtyTracker::Clear() {
    dirty_.clear();
}

void DirtyTracker::SetFullRect(const Rect& rect) {
    full_rect_ = rect;
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
    const bool need_layout = node->IsLayoutDirty() || !node->IsLayoutValid() || !RectEquals(node->RectInParent(), area);
    if (need_layout) {
        node->Layout(area);
    }

    const Rect parent_rect = node->RectInParent();
    for (const auto& child : node->Children()) {
        Rect child_rect{};
        switch (child->GetLayoutMode()) {
            case Widget::LayoutMode::Fixed:
                child_rect = child->DeclaredRect();
                break;
            case Widget::LayoutMode::MatchParent:
                child_rect = {0, 0, parent_rect.w, parent_rect.h};
                break;
            case Widget::LayoutMode::WrapContent: {
                const Rect declared = child->DeclaredRect();
                const Size measured = child->Measure({parent_rect.w, parent_rect.h});
                child_rect = {declared.x, declared.y, measured.w, measured.h};
                break;
            }
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

void RenderList::Traverse(Widget* node, uint32_t depth, uint32_t& order) {
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
    ::app_ui::debug::PrintRenderItem(items_.back());

    for (const auto& child : node->Children()) {
        Traverse(child.get(), depth + 1, order);
    }
}

const RenderCapabilities& Renderer::Capabilities() const {
    return caps_;
}

void Renderer::Render(RenderList& list, DirtyTracker& dirty, Painter& painter) {
    if (!dirty.HasDirty()) {
        return;
    }
    // Clipping is not supported by current Painter implementations.
    const bool clip_to_dirty = !dirty.RequireFullRefresh();
    if (clip_to_dirty) {
        painter.SetDrawColor(Color::White);
        for (const auto& item : dirty.Items()) {
            painter.FillRect(item.rect);
        }
        painter.SetDrawColor(Color::Black);
    }
    for (const auto& item : list.Items()) {
        if (!item.widget) {
            continue;
        }
        if (clip_to_dirty && !dirty.Intersects(item.rect)) {
            ::app_ui::debug::PrintRenderSkip(item, "not_in_dirty");
            continue;
        }
        Rect local_dirty{0, 0, item.rect.w, item.rect.h};
        if (clip_to_dirty) {
            Rect merged{};
            bool has_dirty = false;
            for (const auto& dirty_item : dirty.Items()) {
                if (!dirty_item.rect.Intersects(item.rect)) {
                    continue;
                }
                Rect intersect = RectIntersect(item.rect, dirty_item.rect);
                if (intersect.IsEmpty()) {
                    continue;
                }
                intersect.x = static_cast<int16_t>(intersect.x - item.rect.x);
                intersect.y = static_cast<int16_t>(intersect.y - item.rect.y);
                if (!has_dirty) {
                    merged = intersect;
                    has_dirty = true;
                } else {
                    merged = merged.Union(intersect);
                }
            }
            if (has_dirty) {
                local_dirty = merged;
            }
        }

        painter.SetTransform({item.rect.x, item.rect.y});
        ::app_ui::debug::PrintRenderItem(item);
        item.widget->Draw(painter, local_dirty);
        painter.SetTransform({0, 0});
    }

    dirty.Clear();
}

} // namespace app_ui
