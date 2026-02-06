#pragma once

#include <memory>
#include <string>
#include <vector>

#include "input.h"
#include "widget.h"

struct cJSON;

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

class UiSchemaValidator {
public:
    static bool Validate(const cJSON* root, std::string& error);
};

namespace runtime {

struct SceneRuntime {
    uint16_t scene_id = 0;
    std::shared_ptr<Widget> root;
    FocusManager focus;
};

class SceneManager {
public:
    bool LoadFromJson(const cJSON* root, const char* scene_id, uint16_t scene_index);

    Widget* Root() const;
    std::shared_ptr<Widget> RootShared() const;
    FocusManager& Focus();
    uint16_t SceneId() const;

private:
    SceneRuntime scene_{};
};

} // namespace runtime

} // namespace app_ui
