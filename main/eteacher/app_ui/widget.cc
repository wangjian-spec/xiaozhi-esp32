#include "widget.h"
#include "painter.h"

namespace app_ui {

Widget* Widget::AddChild(std::unique_ptr<Widget> child) {
    if (!child) {
        return nullptr;
    }
    child->parent_ = this;
    Widget* raw = child.get();
    children_.push_back(std::move(child));
    flags_.layout_dirty = 1;
    flags_.dirty = 1;
    return raw;
}

Widget* Widget::Parent() const {
    return parent_;
}

const std::vector<std::unique_ptr<Widget>>& Widget::Children() const {
    return children_;
}

Size Widget::Measure(const Size& constraint) {
    if (cache_.measure_valid && cache_.last_constraint == constraint) {
        return cache_.measured;
    }
    cache_.measured = OnMeasure(constraint);
    cache_.last_constraint = constraint;
    cache_.measure_valid = true;
    cache_.measure_version++;
    return cache_.measured;
}

void Widget::Layout(const Rect& rect) {
    cache_.layout = rect;
    cache_.layout_valid = true;
    cache_.layout_version++;
    flags_.layout_dirty = 0;
    OnLayout(rect);
}

void Widget::Draw(Painter& p) {
    if (!Visible()) {
        return;
    }
    OnDraw(p);
    flags_.dirty = 0;
}

void Widget::MarkDirty() {
    flags_.dirty = 1;
}

void Widget::MarkLayoutDirty() {
    flags_.layout_dirty = 1;
    cache_.layout_valid = false;
    cache_.measure_valid = false;
    if (parent_) {
        parent_->MarkLayoutDirty();
    }
}

void Widget::SetVisible(bool v) {
    if (flags_.visible == static_cast<uint8_t>(v)) {
        return;
    }
    flags_.visible = v ? 1 : 0;
    MarkDirty();
}

bool Widget::Visible() const {
    return flags_.visible != 0;
}

void Widget::SetEnabled(bool v) {
    if (flags_.enabled == static_cast<uint8_t>(v)) {
        return;
    }
    flags_.enabled = v ? 1 : 0;
    MarkDirty();
}

void Widget::SetFocusable(bool v) {
    if (flags_.focusable == static_cast<uint8_t>(v)) {
        return;
    }
    flags_.focusable = v ? 1 : 0;
    MarkDirty();
}

bool Widget::Enabled() const {
    return flags_.enabled != 0;
}

Rect Widget::RectInParent() const {
    return cache_.layout;
}

void Widget::SetRectInParent(const Rect& rect) {
    cache_.layout = rect;
    cache_.layout_valid = true;
    flags_.layout_dirty = 0;
}

Rect Widget::RectInWindow() const {
    Point global = MapToGlobal({0, 0});
    Rect rect = cache_.layout;
    rect.x = global.x;
    rect.y = global.y;
    return rect;
}

Rect Widget::RectInScreen() const {
    return RectInWindow();
}

Point Widget::MapToGlobal(Point local) const {
    Point p = local;
    const Widget* current = this;
    while (current) {
        p.x = static_cast<int16_t>(p.x + current->cache_.layout.x);
        p.y = static_cast<int16_t>(p.y + current->cache_.layout.y);
        current = current->parent_;
    }
    return p;
}

Point Widget::MapFromGlobal(Point global) const {
    Point p = global;
    const Widget* current = this;
    while (current) {
        p.x = static_cast<int16_t>(p.x - current->cache_.layout.x);
        p.y = static_cast<int16_t>(p.y - current->cache_.layout.y);
        current = current->parent_;
    }
    return p;
}

bool Widget::HitTest(Point global) const {
    if (!Visible()) {
        return false;
    }
    Rect rect = RectInWindow();
    return rect.Contains(global);
}

} // namespace app_ui
