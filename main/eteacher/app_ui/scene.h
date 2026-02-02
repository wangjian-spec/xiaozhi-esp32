#pragma once

#include <memory>
#include <string>
#include <vector>

#include "widget.h"

namespace app_ui {

class Scene {
public:
    virtual ~Scene() = default;

    virtual Widget* BuildUI() = 0;
    virtual const char* Name() const { return ""; }

    Widget* Root() const { return root_.get(); }

    virtual void OnEnter() {}
    virtual void OnExit() {}
    virtual void OnPause() {}
    virtual void OnResume() {}

protected:
    void SetRoot(std::unique_ptr<Widget> root) { root_ = std::move(root); }

private:
    std::unique_ptr<Widget> root_{};
};

class SceneManager {
public:
    void Push(std::unique_ptr<Scene> scene);
    void Pop();
    void Replace(std::unique_ptr<Scene> scene);
    Scene* FindByName(const std::string& name);
    Scene* Current();
    void Clear();
    bool PromoteToTop(const std::string& name);

private:
    std::vector<std::unique_ptr<Scene>> stack_;
};

} // namespace app_ui
