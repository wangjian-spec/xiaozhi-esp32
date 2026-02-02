#include "input.h"

#include "focus_manager.h"
#include "widget.h"

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

size_t InputQueue::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
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

    DispatchPath(path, event);
}

bool InputDispatcher::DispatchPath(const std::vector<Widget*>& path, const InputEvent& event) {
    if (path.empty()) {
        return false;
    }

    for (size_t i = 0; i + 1 < path.size(); ++i) {
        if (path[i]->OnInput(event)) {
            return true;
        }
    }

    if (path.back()->OnInput(event)) {
        return true;
    }

    for (size_t i = path.size(); i-- > 1;) {
        if (path[i - 1]->OnInput(event)) {
            return true;
        }
    }
    return false;
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
