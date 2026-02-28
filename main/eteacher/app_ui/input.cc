#include "input.h"

// 输入处理模块
// 本文件实现了输入事件队列、焦点管理等逻辑，用于将按键/触摸等输入分发到 UI widget。
// 注意：本模块无关 UI 描述加载方式，仅负责事件派发和焦点管理。

#include "widget.h"
#include "debug.h"

#include <algorithm>
#include <cstdio>

namespace app_ui {

namespace {

bool IsPointerEvent(InputType type) {
    return type == InputType::PointerDown || type == InputType::PointerUp || type == InputType::PointerMove;
}

} // namespace

void InputQueue::Push(const InputEvent& e) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(e);
}

bool InputQueue::TryPop(InputEvent& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        return false;
    }
    out = queue_.front();
    queue_.pop();
    return true;
}

void InputQueue::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty()) {
        queue_.pop();
    }
}

void FocusManager::Build(Widget* root) {
    focus_ids_.clear();
    focus_index_by_id_.clear();
    id_to_widget_.clear();
    root_ = root;
    current_index_ = -1;
    Traverse(root);
    if (!focus_ids_.empty()) {
        current_index_ = 0;
        if (Widget* current = ResolveByIndex(current_index_)) {
            current->SetFocused(true);
        }
    }
    dirty_ = false;
}

void FocusManager::Clear() {
    if (Widget* current = ResolveByIndex(current_index_)) {
        current->SetFocused(false);
    }
    focus_ids_.clear();
    focus_index_by_id_.clear();
    id_to_widget_.clear();
    current_index_ = -1;
    dirty_ = true;
    root_ = nullptr;
}

void FocusManager::RebuildIfNeeded(Widget* root) {
    if (!dirty_) {
        return;
    }
    Clear();
    if (root) {
        Build(root);
    } else {
        dirty_ = false;
    }
}

bool FocusManager::HandleDirectionalKey(KeyCode key) {
    if (key != KeyCode::Up && key != KeyCode::Down && key != KeyCode::Left && key != KeyCode::Right) {
        return false;
    }
    Widget* target = Current();
    if (!target) {
        return false;
    }
    const FocusIntent intent = target->OnFocusKey(key);
    switch (intent) {
        case FocusIntent::Consume:
            return true;
        case FocusIntent::EscapeUp:
            return MoveSpatial(KeyCode::Up);
        case FocusIntent::EscapeDown:
            return MoveSpatial(KeyCode::Down);
        case FocusIntent::EscapeLeft:
            return MoveSpatial(KeyCode::Left);
        case FocusIntent::EscapeRight:
            return MoveSpatial(KeyCode::Right);
        case FocusIntent::Bubble:
        case FocusIntent::None:
        default:
            return MoveSpatial(key);
    }
}

bool FocusManager::SetCurrentById(uint32_t id) {
    if (!root_ || id == 0) {
        return false;
    }
    Widget* target = ResolveById(id);
    app_ui::debug::PrintFocusSetById(id, target);
    return SetCurrent(target);
}

Widget* FocusManager::Current() const {
    return ResolveByIndex(current_index_);
}

bool FocusManager::MoveSpatial(KeyCode key) {
    Widget* target = Current();
    Widget* spatial = FindSpatialTarget(target, key);
    if (spatial) {
        return SetCurrent(spatial);
    }
    if (!wrap_) {
        return false;
    }
    if (key == KeyCode::Up || key == KeyCode::Left) {
        MoveLinear(-1);
        return true;
    }
    if (key == KeyCode::Down || key == KeyCode::Right) {
        MoveLinear(1);
        return true;
    }
    return false;
}

void FocusManager::MoveLinear(int delta) {
    if (focus_ids_.empty()) {
        return;
    }
    int next = current_index_ + delta;
    if (wrap_) {
        if (next < 0) {
            next = static_cast<int>(focus_ids_.size()) - 1;
        }
        if (next >= static_cast<int>(focus_ids_.size())) {
            next = 0;
        }
    } else {
        if (next < 0 || next >= static_cast<int>(focus_ids_.size())) {
            return;
        }
    }
    if (next == current_index_) {
        return;
    }
    if (Widget* current = ResolveByIndex(current_index_)) {
        current->SetFocused(false);
    }
    current_index_ = next;
    if (Widget* current = ResolveByIndex(current_index_)) {
        current->SetFocused(true);
    }
}

bool FocusManager::SetCurrent(Widget* target) {
    if (!target) {
        printf("[FocusManager] SetCurrent called with null target\n");
        return false;
    }
    const uint32_t id = target->Id();
    if (id == 0) {
        return false;
    }
    Widget* cur = ResolveByIndex(current_index_);
    const uint32_t cur_id = cur ? cur->Id() : 0;
    app_ui::debug::PrintFocusSwitch(cur_id, cur, id, target);
    auto it = focus_index_by_id_.find(id);
    if (it == focus_index_by_id_.end()) {
        return false;
    }
    const int next = it->second;
    if (next == current_index_) {
        return true;
    }
    if (Widget* current = ResolveByIndex(current_index_)) {
        current->SetFocused(false);
    }
    current_index_ = next;
    if (Widget* current = ResolveByIndex(current_index_)) {
        current->SetFocused(true);
    }
    return true;
}

Widget* FocusManager::FindSpatialTarget(Widget* current, KeyCode key) const {
    if (!current) {
        return nullptr;
    }
    if (focus_ids_.empty()) {
        return nullptr;
    }

    const Rect current_rect = current->RectInWindow();
    const int32_t cx = current_rect.x + current_rect.w / 2;
    const int32_t cy = current_rect.y + current_rect.h / 2;

    Widget* best = nullptr;
    int32_t best_score = INT32_MAX;

    for (uint32_t candidate_id : focus_ids_) {
        Widget* candidate = ResolveById(candidate_id);
        if (!candidate || candidate == current) {
            continue;
        }
        const Rect rect = candidate->RectInWindow();
        const int32_t tx = rect.x + rect.w / 2;
        const int32_t ty = rect.y + rect.h / 2;
        const int32_t dx = tx - cx;
        const int32_t dy = ty - cy;

        bool in_dir = false;
        int32_t primary = 0;
        int32_t secondary = 0;
        switch (key) {
            case KeyCode::Up:
                in_dir = dy < 0;
                primary = -dy;
                secondary = dx < 0 ? -dx : dx;
                break;
            case KeyCode::Down:
                in_dir = dy > 0;
                primary = dy;
                secondary = dx < 0 ? -dx : dx;
                break;
            case KeyCode::Left:
                in_dir = dx < 0;
                primary = -dx;
                secondary = dy < 0 ? -dy : dy;
                break;
            case KeyCode::Right:
                in_dir = dx > 0;
                primary = dx;
                secondary = dy < 0 ? -dy : dy;
                break;
            default:
                break;
        }
        if (!in_dir) {
            continue;
        }

        const int32_t score = primary * 1024 + secondary;
        if (score < best_score) {
            best_score = score;
            best = candidate;
        }
    }

    return best;
}

void FocusManager::Traverse(Widget* node) {
    if (!node || !node->Visible()) {
        return;
    }
    const uint32_t id = node->Id();
    if (id != 0) {
        id_to_widget_[id] = node;
    }
    if (node->Focusable() && node->Enabled()) {
        if (id != 0) {
            focus_index_by_id_[id] = static_cast<int>(focus_ids_.size());
            focus_ids_.push_back(id);
        }
    }
    for (const auto& child : node->Children()) {
        Traverse(child.get());
    }
}

Widget* FocusManager::ResolveById(uint32_t id) const {
    auto it = id_to_widget_.find(id);
    if (it == id_to_widget_.end()) {
        return nullptr;
    }
    return it->second;
}

Widget* FocusManager::ResolveByIndex(int index) const {
    if (!root_ || index < 0 || index >= static_cast<int>(focus_ids_.size())) {
        return nullptr;
    }
    return ResolveById(focus_ids_[index]);
}

void InputDispatcher::Dispatch(const InputEvent& event, Widget* root, FocusManager& focus) {
    if (!root) {
        return;
    }

    std::vector<Widget*> path;
    if (IsPointerEvent(event.type)) {
        if (!FindPathHit(root, event.pos, path)) {
            return;
        }
    } else {
        Widget* target = focus.Current();
        if (!target) {
            target = root;
        }
        if (!FindPathTo(root, target, path)) {
            return;
        }
    }

    const bool handled = DispatchPath(path, event);
    if (handled) {
        return;
    }

    if (event.type != InputType::KeyDown && event.type != InputType::KeyRepeat) {
        return;
    }

    focus.HandleDirectionalKey(static_cast<KeyCode>(event.key));
}

bool InputDispatcher::DispatchPath(const std::vector<Widget*>& path, const InputEvent& event) {
    if (path.empty()) {
        return false;
    }

    bool handled = false;
    bool stop_bubble = false;

    // Capture: Consume immediately handles the event; StopBubble blocks bubbling but still runs target.
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const InputResult result = path[i]->OnInput(event, InputPhase::Capture);
        if (result == InputResult::Consume) {
            return true;
        }
        if (result == InputResult::StopBubble) {
            handled = true;
            stop_bubble = true;
            break;
        }
    }

    const InputResult target_result = path.back()->OnInput(event, InputPhase::Target);
    if (target_result == InputResult::Consume) {
        return true;
    }
    if (target_result == InputResult::StopBubble) {
        handled = true;
        stop_bubble = true;
    }

    if (!stop_bubble) {
        // Bubble: allow Consume or StopBubble.
        for (size_t i = path.size(); i-- > 1;) {
            const InputResult result = path[i - 1]->OnInput(event, InputPhase::Bubble);
            if (result == InputResult::Consume) {
                return true;
            }
            if (result == InputResult::StopBubble) {
                handled = true;
                break;
            }
        }
    }

    return handled;
}

bool InputDispatcher::FindPathTo(Widget* node, Widget* target, std::vector<Widget*>& path) {
    if (!node) {
        return false;
    }
    path.push_back(node);
    if (node == target) {
        return true;
    }
    for (const auto& child : node->Children()) {
        if (FindPathTo(child.get(), target, path)) {
            return true;
        }
    }
    path.pop_back();
    return false;
}

bool InputDispatcher::FindPathHit(Widget* node, Point global, std::vector<Widget*>& path) {
    if (!node || !node->Visible()) {
        return false;
    }

    if (!node->HitTest(global)) {
        return false;
    }

    path.push_back(node);
    for (auto it = node->Children().rbegin(); it != node->Children().rend(); ++it) {
        if (FindPathHit(it->get(), global, path)) {
            return true;
        }
    }
    return true;
}

} // namespace app_ui
