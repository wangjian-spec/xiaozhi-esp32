#include "focus_manager.h"
#include "widget.h"

namespace app_ui {

void FocusManager::Build(Widget* root) {
    focusables_.clear();
    current_index_ = -1;
    Traverse(root);
    if (!focusables_.empty()) {
        current_index_ = 0;
    }
}

void FocusManager::MoveUp() {
    Move(-1);
}

void FocusManager::MoveDown() {
    Move(1);
}

void FocusManager::MoveLeft() {
    Move(-1);
}

void FocusManager::MoveRight() {
    Move(1);
}

Widget* FocusManager::Current() const {
    if (current_index_ < 0 || current_index_ >= static_cast<int>(focusables_.size())) {
        return nullptr;
    }
    return focusables_[current_index_];
}

void FocusManager::SetWrap(bool wrap) {
    wrap_ = wrap;
}

void FocusManager::Move(int delta) {
    if (focusables_.empty()) {
        return;
    }
    int next = current_index_ + delta;
    if (wrap_) {
        if (next < 0) {
            next = static_cast<int>(focusables_.size()) - 1;
        }
        if (next >= static_cast<int>(focusables_.size())) {
            next = 0;
        }
    } else {
        if (next < 0 || next >= static_cast<int>(focusables_.size())) {
            return;
        }
    }
    current_index_ = next;
}

void FocusManager::Traverse(Widget* node) {
    if (!node || !node->Visible()) {
        return;
    }
    if (node->Focusable() && node->Enabled()) {
        focusables_.push_back(node);
    }
    for (const auto& child : node->Children()) {
        Traverse(child.get());
    }
}

} // namespace app_ui
