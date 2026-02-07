#include "ui_router.h"

void UiRouter::Reset() {
    scene_ids_.clear();
    index_ = 0;
    activate_ = nullptr;
}

void UiRouter::SetScenes(std::vector<std::string> scene_ids) {
    scene_ids_ = std::move(scene_ids);
    index_ = 0;
}

void UiRouter::SetActivateFn(ActivateFn fn) {
    activate_ = std::move(fn);
}

bool UiRouter::HasScenes() const {
    return !scene_ids_.empty();
}

size_t UiRouter::Index() const {
    return index_;
}

const std::string& UiRouter::CurrentId() const {
    static const std::string kEmpty;
    if (scene_ids_.empty()) {
        return kEmpty;
    }
    return scene_ids_[index_];
}

bool UiRouter::Activate(AppContext& ctx, size_t index) {
    if (!activate_ || scene_ids_.empty() || index >= scene_ids_.size()) {
        return false;
    }
    index_ = index;
    return activate_(ctx, index_, scene_ids_[index_]);
}

bool UiRouter::Next(AppContext& ctx) {
    if (scene_ids_.empty()) {
        return false;
    }
    const size_t next_index = (index_ + 1) % scene_ids_.size();
    return Activate(ctx, next_index);
}

bool UiRouter::Prev(AppContext& ctx) {
    if (scene_ids_.empty()) {
        return false;
    }
    const size_t prev_index = (index_ + scene_ids_.size() - 1) % scene_ids_.size();
    return Activate(ctx, prev_index);
}
