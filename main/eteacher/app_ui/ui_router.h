#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "eteacher/app_manager/app_base.h"

// 场景路由接口
// 声明了用于在应用内管理和切换场景的简单路由器（UiRouter），
// 它维护场景 id 列表并通过回调激活目标场景。

namespace app_ui {

class UiRouter {
public:
    using ActivateFn = std::function<bool(AppContext& ctx, size_t index, const std::string& scene_id)>;
    using EnterFn = std::function<bool(AppContext& ctx)>;
    using ExitFn = std::function<void(AppContext& ctx)>;

    enum class RouterMode : uint8_t {
        Unset,
        Linear,
        Stack,
    };

    struct SceneRoute {
        std::string id;
        EnterFn on_enter;
        ExitFn on_exit;
    };

    void Reset();
    void SetScenes(std::vector<std::string> scene_ids);
    void SetActivateFn(ActivateFn fn);
    void SetRoutes(std::vector<SceneRoute> routes);

    bool HasScenes() const;
    size_t Index() const;
    const std::string& CurrentId() const;

    bool Activate(AppContext& ctx, size_t index);
    bool Push(AppContext& ctx, const std::string& id);
    bool Pop(AppContext& ctx);
    bool Next(AppContext& ctx);
    bool Prev(AppContext& ctx);

private:
    std::vector<std::string> scene_ids_{};
    size_t index_ = 0;
    ActivateFn activate_{};
    std::vector<SceneRoute> routes_{};
    std::vector<size_t> route_stack_{};
    RouterMode mode_ = RouterMode::Unset;
};

} // namespace app_ui

using UiRouter = app_ui::UiRouter;
