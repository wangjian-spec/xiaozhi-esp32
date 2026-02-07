#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "eteacher/app_manager/app_base.h"

class UiRouter {
public:
    using ActivateFn = std::function<bool(AppContext& ctx, size_t index, const std::string& scene_id)>;

    void Reset();
    void SetScenes(std::vector<std::string> scene_ids);
    void SetActivateFn(ActivateFn fn);

    bool HasScenes() const;
    size_t Index() const;
    const std::string& CurrentId() const;

    bool Activate(AppContext& ctx, size_t index);
    bool Next(AppContext& ctx);
    bool Prev(AppContext& ctx);

private:
    std::vector<std::string> scene_ids_{};
    size_t index_ = 0;
    ActivateFn activate_{};
};
