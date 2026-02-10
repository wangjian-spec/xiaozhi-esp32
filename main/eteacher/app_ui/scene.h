#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ui_desc.h"
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

std::vector<std::string> CollectSceneIds(const desc::UiDesc& ui);

namespace runtime {

class SceneRuntime {
public:
    // 生产（静态）：从生成的 `UiDesc` 结构加载 UI。
    bool LoadFromDesc(const desc::UiDesc& ui, const char* scene_id, uint16_t scene_index);

    Widget* Root() const;
    std::unique_ptr<Widget> TakeRoot();
    uint16_t SceneId() const;

private:
    uint16_t scene_id_ = 0;
    std::unique_ptr<Widget> root_{};
};

} // namespace runtime

} // namespace app_ui
