// 场景路由实现
// 负责管理可用场景列表、当前索引及激活回调。UiRouter 用于在 app 内切换场景。

#include "ui_router.h"

namespace app_ui {

void UiRouter::Reset() {
    scene_ids_.clear();
    index_ = 0;
    activate_ = nullptr;
    routes_.clear();
    route_stack_.clear();
    mode_ = RouterMode::Unset;
}

void UiRouter::SetScenes(std::vector<std::string> scene_ids) {
    scene_ids_ = std::move(scene_ids);
    index_ = 0;
    routes_.clear();
    route_stack_.clear();
    mode_ = RouterMode::Linear;
}

void UiRouter::SetActivateFn(ActivateFn fn) {
    if (mode_ != RouterMode::Linear) {
        return;
    }
    activate_ = std::move(fn);
}

void UiRouter::SetRoutes(std::vector<SceneRoute> routes) {
    routes_ = std::move(routes);
    route_stack_.clear();
    scene_ids_.clear();
    activate_ = nullptr;
    mode_ = RouterMode::Stack;
}

bool UiRouter::HasScenes() const {
    if (mode_ == RouterMode::Stack) {
        return !routes_.empty();
    }
    if (mode_ == RouterMode::Linear) {
        return !scene_ids_.empty();
    }
    return false;
}

size_t UiRouter::Index() const {
    if (mode_ == RouterMode::Stack) {
        if (route_stack_.empty()) {
            return 0;
        }
        return route_stack_.back();
    }
    if (mode_ == RouterMode::Linear) {
        return index_;
    }
    return 0;
}

const std::string& UiRouter::CurrentId() const {
    static const std::string kEmpty;
    if (mode_ == RouterMode::Stack) {
        if (route_stack_.empty()) {
            return kEmpty;
        }
        const size_t idx = route_stack_.back();
        if (idx >= routes_.size()) {
            return kEmpty;
        }
        return routes_[idx].id;
    }
    if (mode_ == RouterMode::Linear) {
        if (scene_ids_.empty()) {
            return kEmpty;
        }
        return scene_ids_[index_];
    }
    return kEmpty;
}

bool UiRouter::Activate(AppContext& ctx, size_t index) {
    if (mode_ == RouterMode::Stack) {
        if (index >= routes_.size()) {
            return false;
        }
        const size_t prev_index = route_stack_.empty() ? static_cast<size_t>(-1) : route_stack_.back();
        if (prev_index < routes_.size() && routes_[prev_index].on_exit) {
            routes_[prev_index].on_exit(ctx);
        }
        bool ok = true;
        if (routes_[index].on_enter) {
            ok = routes_[index].on_enter(ctx);
        }
        if (!ok) {
            if (prev_index < routes_.size() && routes_[prev_index].on_enter) {
                routes_[prev_index].on_enter(ctx);
            }
            return false;
        }
        route_stack_.clear();
        route_stack_.push_back(index);
        return true;
    }
    if (mode_ != RouterMode::Linear) {
        return false;
    }
    if (!activate_ || scene_ids_.empty() || index >= scene_ids_.size()) {
        return false;
    }
    index_ = index;
    return activate_(ctx, index_, scene_ids_[index_]);
}

bool UiRouter::Push(AppContext& ctx, const std::string& id) {
    if (mode_ != RouterMode::Stack || routes_.empty()) {
        return false;
    }
    size_t target = static_cast<size_t>(-1);
    for (size_t i = 0; i < routes_.size(); ++i) {
        if (routes_[i].id == id) {
            target = i;
            break;
        }
    }
    if (target >= routes_.size()) {
        return false;
    }
    const size_t prev_index = route_stack_.empty() ? static_cast<size_t>(-1) : route_stack_.back();
    if (prev_index < routes_.size() && routes_[prev_index].on_exit) {
        routes_[prev_index].on_exit(ctx);
    }
    bool ok = true;
    if (routes_[target].on_enter) {
        ok = routes_[target].on_enter(ctx);
    }
    if (!ok) {
        if (prev_index < routes_.size() && routes_[prev_index].on_enter) {
            routes_[prev_index].on_enter(ctx);
        }
        return false;
    }
    route_stack_.push_back(target);
    return true;
}

bool UiRouter::Pop(AppContext& ctx) {
    if (mode_ != RouterMode::Stack || route_stack_.size() <= 1) {
        return false;
    }
    const size_t current = route_stack_.back();
    if (current < routes_.size() && routes_[current].on_exit) {
        routes_[current].on_exit(ctx);
    }
    route_stack_.pop_back();
    const size_t next = route_stack_.back();
    bool ok = true;
    if (next < routes_.size() && routes_[next].on_enter) {
        ok = routes_[next].on_enter(ctx);
    }
    if (!ok) {
        route_stack_.push_back(current);
        if (current < routes_.size() && routes_[current].on_enter) {
            routes_[current].on_enter(ctx);
        }
        return false;
    }
    return true;
}

bool UiRouter::Next(AppContext& ctx) {
    if (mode_ == RouterMode::Stack) {
        if (route_stack_.empty()) {
            return false;
        }
        const size_t next_index = (route_stack_.back() + 1) % routes_.size();
        return Activate(ctx, next_index);
    }
    if (mode_ != RouterMode::Linear || scene_ids_.empty()) {
        return false;
    }
    const size_t next_index = (index_ + 1) % scene_ids_.size();
    return Activate(ctx, next_index);
}

bool UiRouter::Prev(AppContext& ctx) {
    if (mode_ == RouterMode::Stack) {
        if (route_stack_.empty()) {
            return false;
        }
        const size_t prev_index = (route_stack_.back() + routes_.size() - 1) % routes_.size();
        return Activate(ctx, prev_index);
    }
    if (mode_ != RouterMode::Linear || scene_ids_.empty()) {
        return false;
    }
    const size_t prev_index = (index_ + scene_ids_.size() - 1) % scene_ids_.size();
    return Activate(ctx, prev_index);
}

} // namespace app_ui
