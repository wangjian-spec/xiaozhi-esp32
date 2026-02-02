#include "scene.h"

namespace app_ui {

void SceneManager::Push(std::unique_ptr<Scene> scene) {
    if (!scene) {
        return;
    }
    if (!stack_.empty()) {
        stack_.back()->OnPause();
    }
    scene->OnEnter();
    stack_.push_back(std::move(scene));
}

void SceneManager::Pop() {
    if (stack_.empty()) {
        return;
    }
    stack_.back()->OnExit();
    stack_.pop_back();
    if (!stack_.empty()) {
        stack_.back()->OnResume();
    }
}

void SceneManager::Replace(std::unique_ptr<Scene> scene) {
    Pop();
    Push(std::move(scene));
}

Scene* SceneManager::FindByName(const std::string& name) {
    for (const auto& scene : stack_) {
        if (scene && name == scene->Name()) {
            return scene.get();
        }
    }
    return nullptr;
}

Scene* SceneManager::Current() {
    if (stack_.empty()) {
        return nullptr;
    }
    return stack_.back().get();
}

void SceneManager::Clear() {
    while (!stack_.empty()) {
        Pop();
    }
}

bool SceneManager::PromoteToTop(const std::string& name) {
    if (stack_.empty()) {
        return false;
    }
    size_t index = stack_.size();
    for (size_t i = 0; i < stack_.size(); ++i) {
        if (stack_[i] && name == stack_[i]->Name()) {
            index = i;
            break;
        }
    }
    if (index >= stack_.size()) {
        return false;
    }
    if (index == stack_.size() - 1) {
        return true;
    }
    if (!stack_.empty()) {
        stack_.back()->OnPause();
    }
    auto scene = std::move(stack_[index]);
    stack_.erase(stack_.begin() + static_cast<long>(index));
    scene->OnResume();
    stack_.push_back(std::move(scene));
    return true;
}

} // namespace app_ui
