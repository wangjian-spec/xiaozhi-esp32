#include "renderer.h"

#include <algorithm>
#include <string_view>

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/font_manager/font_manager.h"
#include "widget.h"

namespace app_ui {

namespace {
constexpr size_t kFullRefreshThreshold = 32;
constexpr const char* kStatusFont = "wenquanyi_9pt";

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
